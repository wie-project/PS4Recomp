// SPDX-License-Identifier: GPL-2.0-or-later

// libSceCamera capture backend: AVFoundation in place of shadPS4's SDL camera. The device is
// asked for YUY2 ('yuvs': Y0 U Y1 V) frames at CAMERA_FRAME_WIDTH x CAMERA_FRAME_HEIGHT, as
// shadPS4 asks SDL; AVFoundation scales other sizes. The delegate keeps the newest frame in a
// buffer allocated once, and acquire hands it out once, like SDL_AcquireCameraFrame.
//
// macOS asks the player for camera access the first time a game starts the camera; the game
// bundle declares NSCameraUsageDescription for it. Without access the device cannot be opened.

#import <AVFoundation/AVFoundation.h>
#include "ps4_camera.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_ROW_BYTES ((size_t)CAMERA_FRAME_WIDTH * 2)
#define FRAME_BYTES (FRAME_ROW_BYTES * CAMERA_FRAME_HEIGHT)

static pthread_mutex_t g_frame_mu = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *g_frame; // newest frame, packed rows
static bool g_frame_new;

@interface CameraCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@end

@implementation CameraCaptureDelegate

- (void)captureOutput:(AVCaptureOutput *)output
    didOutputSampleBuffer:(CMSampleBufferRef)sample
           fromConnection:(AVCaptureConnection *)connection {
    CVPixelBufferRef pb = CMSampleBufferGetImageBuffer(sample);
    if (!pb || CVPixelBufferGetPixelFormatType(pb) != kCVPixelFormatType_422YpCbCr8_yuvs ||
        CVPixelBufferGetWidth(pb) != CAMERA_FRAME_WIDTH || CVPixelBufferGetHeight(pb) != CAMERA_FRAME_HEIGHT) {
        return;
    }
    if (CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return;
    const uint8_t *src = CVPixelBufferGetBaseAddress(pb);
    const size_t stride = CVPixelBufferGetBytesPerRow(pb);
    pthread_mutex_lock(&g_frame_mu);
    if (g_frame && src) {
        if (stride == FRAME_ROW_BYTES) {
            memcpy(g_frame, src, FRAME_BYTES);
        } else {
            for (size_t y = 0; y < CAMERA_FRAME_HEIGHT; y++) {
                memcpy(g_frame + y * FRAME_ROW_BYTES, src + y * stride, FRAME_ROW_BYTES);
            }
        }
        g_frame_new = true;
    }
    pthread_mutex_unlock(&g_frame_mu);
    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
}

@end

static AVCaptureSession *g_session;
static CameraCaptureDelegate *g_delegate;
static dispatch_queue_t g_queue;

// The video capture devices in a stable order (built-in first, then external).
static NSArray<AVCaptureDevice *> *capture_devices(void) {
    NSMutableArray<AVCaptureDeviceType> *types = [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
    if (@available(macOS 14.0, *)) {
        [types addObject:AVCaptureDeviceTypeExternal];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [types addObject:AVCaptureDeviceTypeExternalUnknown];
#pragma clang diagnostic pop
    }
    AVCaptureDeviceDiscoverySession *ds =
        [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
                                                               mediaType:AVMediaTypeVideo
                                                                position:AVCaptureDevicePositionUnspecified];
    return ds.devices;
}

int32_t ps4_camera_capture_device_count(void) {
    @autoreleasepool {
        return (int32_t)capture_devices().count;
    }
}

// Waits for the player's answer when macOS has not asked yet.
static bool camera_access_granted(void) {
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
    case AVAuthorizationStatusAuthorized:
        return true;
    case AVAuthorizationStatusNotDetermined: {
        __block bool granted = false;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                 completionHandler:^(BOOL ok) {
                                   granted = ok;
                                   dispatch_semaphore_signal(done);
                                 }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        return granted;
    }
    default:
        return false;
    }
}

bool ps4_camera_capture_open(int32_t index) {
    @autoreleasepool {
        NSArray<AVCaptureDevice *> *devices = capture_devices();
        if (index < 0 || (NSUInteger)index >= devices.count) return false;
        if (!camera_access_granted()) {
            fprintf(stderr, "[ps4-camera] camera access denied (System Settings > Privacy & Security > Camera)\n");
            return false;
        }
        AVCaptureDevice *device = devices[(NSUInteger)index];
        NSError *err = nil;
        AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&err];
        if (!input) {
            fprintf(stderr, "[ps4-camera] cannot open %s: %s\n", device.localizedName.UTF8String,
                    err.localizedDescription.UTF8String);
            return false;
        }
        pthread_mutex_lock(&g_frame_mu);
        if (!g_frame) g_frame = malloc(FRAME_BYTES);
        g_frame_new = false;
        const bool have_buffer = g_frame != NULL;
        pthread_mutex_unlock(&g_frame_mu);
        if (!have_buffer) return false;

        AVCaptureSession *session = [AVCaptureSession new];
        AVCaptureVideoDataOutput *output = [AVCaptureVideoDataOutput new];
        output.videoSettings = @{
            (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_422YpCbCr8_yuvs),
            (id)kCVPixelBufferWidthKey : @(CAMERA_FRAME_WIDTH),
            (id)kCVPixelBufferHeightKey : @(CAMERA_FRAME_HEIGHT),
        };
        output.alwaysDiscardsLateVideoFrames = YES;
        if (!g_queue) g_queue = dispatch_queue_create("ps4.camera.capture", DISPATCH_QUEUE_SERIAL);
        CameraCaptureDelegate *delegate = [CameraCaptureDelegate new];
        [output setSampleBufferDelegate:delegate queue:g_queue];
        if (![session canAddInput:input] || ![session canAddOutput:output]) {
            fprintf(stderr, "[ps4-camera] %s cannot deliver YUY2 frames\n", device.localizedName.UTF8String);
            return false;
        }
        [session addInput:input];
        [session addOutput:output];
        [session startRunning];
        g_session = session;
        g_delegate = delegate;
        fprintf(stderr, "[ps4-camera] AVFoundation camera: %s, %dx%d YUY2\n", device.localizedName.UTF8String,
                CAMERA_FRAME_WIDTH, CAMERA_FRAME_HEIGHT);
        return true;
    }
}

bool ps4_camera_capture_acquire(uint8_t *dst) {
    pthread_mutex_lock(&g_frame_mu);
    const bool fresh = g_frame_new && g_frame;
    if (fresh) {
        memcpy(dst, g_frame, FRAME_BYTES);
        g_frame_new = false;
    }
    pthread_mutex_unlock(&g_frame_mu);
    return fresh;
}

void ps4_camera_capture_close(void) {
    @autoreleasepool {
        [g_session stopRunning];
        g_session = nil;
        g_delegate = nil;
        pthread_mutex_lock(&g_frame_mu);
        g_frame_new = false;
        pthread_mutex_unlock(&g_frame_mu);
    }
}
