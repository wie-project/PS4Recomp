// SPDX-License-Identifier: GPL-2.0-or-later

// The module table of libSceSysmodule, as dumped from firmware 12.52 by shadPS4
// (src/core/libraries/sysmodule/sysmodule_table.h), converted to C. Included only by
// ps4_sysmodule.c: the entries hold the mutable load state.

#ifndef PS4_SYSMODULE_TABLE_H
#define PS4_SYSMODULE_TABLE_H

#include <stddef.h>
#include <stdint.h>

// One module the library can load. Not the real layout: the fields shadPS4 keeps.
typedef struct SysmoduleEntry {
    uint32_t id;             // the ID games pass (0: not loadable by ID)
    int32_t handle;          // the module handle once loaded
    int32_t is_loaded;       // load count
    int32_t flags;           // SYSMODULE_FLAG_*
    const char *name;        // the PRX/SPRX name without extension
    const uint16_t *to_load; // indices into g_modules_array: the module, then its dependencies
    int32_t num_to_load;
} SysmoduleEntry;

#define SYSMODULE_FLAG_COMMON 0x1      // in /system/common/lib
#define SYSMODULE_FLAG_PRIV 0x2        // in /system/priv/lib
#define SYSMODULE_FLAG_GAME 0x4        // in /app0/sce_module
#define SYSMODULE_FLAG_DEBUG 0x8       // devkit/testkit only
#define SYSMODULE_FLAG_NEO 0x200       // PS4 Pro only
#define SYSMODULE_FLAG_NEO_MODE 0x400  // PS4 Pro in enhanced mode only
#define SYSMODULE_FLAG_COMMON_EX 0x1000
#define SYSMODULE_FLAG_PRIV_EX 0x2000

// Module indices sceSysmodulePreloadModuleForLibkernel loads. The library has three versions
// of this list; shadPS4 uses the third. The second also tells unloadModule which modules stay
// loaded once preloaded.
static const uint32_t g_preload_list_2[38] = {1,    2,    0x24, 0x22, 3,    4,    5,    6,    7,    8,
                            9,    0xb,  0xc,  0xd,  0xe,  0xf,  0x10, 0x11, 0x1f, 0x12,
                            0x23, 0x13, 0x14, 0x27, 0x28, 0x16, 0x17, 0x2a, 0x18, 0x29,
                            0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x25, 0x26, 0x1e};
static const uint32_t g_preload_list_3[38] = {1,    2,    0x24, 0x22, 3,    4,    5,    6,    7,    8,
                            9,    0x25, 0xb,  0xc,  0xd,  0xe,  0xf,  0x10, 0x11, 0x1f,
                            0x12, 0x23, 0x13, 0x14, 0x27, 0x28, 0x16, 0x17, 0x2a, 0x18,
                            0x29, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x26, 0x1e};

// Arrays of modules to load for each module.
// The stored values are valid indices to modules in g_modules_array.
static const uint16_t g_libSceNet_modules[1] = {5};
static const uint16_t g_libSceIpmi_modules[1] = {6};
static const uint16_t g_libSceMbus_modules[2] = {7, 6};
static const uint16_t g_libSceRegMgr_modules[1] = {8};
static const uint16_t g_libSceRtc_modules[1] = {9};
static const uint16_t g_libSceAvSetting_modules[3] = {11, 7, 6};
static const uint16_t g_libSceVideoOut_modules[3] = {12, 11, 7};
static const uint16_t g_libSceGnmDriver_modules[4] = {13, 12, 8, 37};
static const uint16_t g_libSceAudioOut_modules[4] = {14, 11, 7, 6};
static const uint16_t g_libSceAudioIn_modules[4] = {15, 14, 7, 6};
static const uint16_t g_libSceAjm_modules[1] = {16};
static const uint16_t g_libScePad_modules[2] = {17, 7};
static const uint16_t g_libSceDbg_debug_modules[1] = {18};
static const uint16_t g_libSceNetCtl_modules[2] = {19, 6};
static const uint16_t g_libSceHttp_modules[5] = {20, 39, 9, 19, 5};
static const uint16_t g_libSceSsl_modules[3] = {21, 9, 5};
static const uint16_t g_libSceNpCommon_modules[8] = {22, 20, 39, 19, 9, 8, 6, 5};
static const uint16_t g_libSceNpManager_modules[7] = {23, 22, 20, 39, 19, 9, 5};
static const uint16_t g_libSceNpWebApi_modules[7] = {24, 23, 22, 20, 39, 9, 5};
static const uint16_t g_libSceSaveData_modules[4] = {25, 27, 9, 6};
static const uint16_t g_libSceSystemService_modules[3] = {26, 8, 6};
static const uint16_t g_libSceUserService_modules[2] = {27, 6};
static const uint16_t g_libSceCommonDialog_modules[1] = {28};
static const uint16_t g_libSceSysUtil_modules[2] = {29, 8};
static const uint16_t g_libScePerf_debug_modules[3] = {30, 38, 37};
static const uint16_t g_libSceCamera_modules[2] = {31, 7};
static const uint16_t g_libSceDiscMap_modules[1] = {34};
static const uint16_t g_libSceDbgAssist_modules[1] = {35};
static const uint16_t g_libSceMat_debug_modules[1] = {36};
static const uint16_t g_libSceRazorCpu_modules[1] = {37};
static const uint16_t g_libSceRazorCpu_debug_debug_modules[2] = {38, 37};
static const uint16_t g_libSceSsl2_modules[3] = {39, 9, 5};
static const uint16_t g_libSceHttp2_modules[13] = {40, 20, 39, 9, 19, 5, 39, 9, 5, 9, 19, 6, 5};
static const uint16_t g_libSceNpWebApi2_modules[39] = {41, 23, 22, 20, 39, 19, 9,  5,  22, 20, 39, 19, 9,
                                     8,  6,  5,  40, 20, 39, 9,  19, 5,  39, 9,  5,  9,
                                     19, 6,  5,  20, 39, 9,  19, 5,  39, 9,  5,  9,  5};
static const uint16_t g_libSceNpGameIntent_modules[10] = {42, 22, 20, 39, 19, 9, 8, 6, 5, 6};
static const uint16_t g_libSceFiber_modules[5] = {49, 114, 30, 38, 37};
static const uint16_t g_libSceUlt_modules[6] = {50, 49, 114, 30, 38, 37};
static const uint16_t g_libSceNgs2_modules[2] = {51, 16};
static const uint16_t g_libSceXml_modules[1] = {52};
static const uint16_t g_libSceNpUtility_modules[5] = {53, 22, 20, 19, 5};
static const uint16_t g_libSceVoice_modules[4] = {54, 16, 15, 14};
static const uint16_t g_libSceNpMatching2_modules[7] = {55, 23, 22, 20, 39, 19, 5};
static const uint16_t g_libSceNpScoreRanking_modules[3] = {56, 23, 22};
static const uint16_t g_libSceRudp_modules[1] = {57};
static const uint16_t g_libSceNpTus_modules[3] = {58, 23, 22};
static const uint16_t g_libSceFace_modules[1] = {59};
static const uint16_t g_libSceSmart_modules[1] = {60};
static const uint16_t g_libSceJson_modules[1] = {61};
static const uint16_t g_libSceGameLiveStreaming_modules[2] = {62, 6};
static const uint16_t g_libSceCompanionUtil_modules[3] = {63, 7, 6};
static const uint16_t g_libScePlayGo_modules[1] = {64};
static const uint16_t g_libSceFont_modules[1] = {65};
static const uint16_t g_libSceVideoRecording_modules[2] = {66, 82};
static const uint16_t g_libSceAudiodec_modules[2] = {67, 16};
static const uint16_t g_libSceJpegDec_modules[1] = {68};
static const uint16_t g_libSceJpegEnc_modules[1] = {69};
static const uint16_t g_libScePngDec_modules[1] = {70};
static const uint16_t g_libScePngEnc_modules[1] = {71};
static const uint16_t g_libSceVideodec_modules[3] = {72, 80, 161};
static const uint16_t g_libSceMove_modules[1] = {73};
static const uint16_t g_libScePadTracker_modules[2] = {75, 17};
static const uint16_t g_libSceDepth_modules[2] = {76, 31};
static const uint16_t g_libSceHand_modules[1] = {77};
static const uint16_t g_libSceIme_modules[2] = {78, 6};
static const uint16_t g_libSceImeDialog_modules[2] = {79, 6};
static const uint16_t g_libSceVdecCore_modules[1] = {80};
static const uint16_t g_libSceNpParty_modules[2] = {81, 6};
static const uint16_t g_libSceAvcap_modules[2] = {82, 6};
static const uint16_t g_libSceFontFt_modules[1] = {83};
static const uint16_t g_libSceFreeTypeOt_modules[1] = {84};
static const uint16_t g_libSceFreeTypeOl_modules[1] = {85};
static const uint16_t g_libSceFreeTypeOptOl_modules[1] = {86};
static const uint16_t g_libSceScreenShot_modules[3] = {87, 29, 6};
static const uint16_t g_libSceNpAuth_modules[3] = {88, 22, 23};
static const uint16_t g_libSceVoiceQos_modules[5] = {89, 54, 16, 15, 14};
static const uint16_t g_libSceSysCore_modules[2] = {90, 6};
static const uint16_t g_libSceM4aacEnc_modules[2] = {91, 16};
static const uint16_t g_libSceAudiodecCpu_modules[1] = {92};
static const uint16_t g_libSceCdlgUtilServer_modules[2] = {93, 26};
static const uint16_t g_libSceSulpha_debug_modules[1] = {94};
static const uint16_t g_libSceSaveDataDialog_modules[4] = {95, 9, 28, 26};
static const uint16_t g_libSceInvitationDialog_modules[1] = {96};
static const uint16_t g_libSceKeyboard_debug_modules[1] = {97};
static const uint16_t g_libSceKeyboard_modules[1] = {98};
static const uint16_t g_libSceMsgDialog_modules[1] = {99};
static const uint16_t g_libSceAvPlayer_modules[1] = {100};
static const uint16_t g_libSceContentExport_modules[1] = {101};
static const uint16_t g_libSceVisionManager_modules[1] = {102};
static const uint16_t g_libSceAc3Enc_modules[2] = {103, 16};
static const uint16_t g_libSceAppInstUtil_modules[1] = {104};
static const uint16_t g_libSceVencCore_modules[1] = {105};
static const uint16_t g_libSceAudio3d_modules[1] = {106};
static const uint16_t g_libSceNpCommerce_modules[1] = {107};
static const uint16_t g_libSceHidControl_modules[1] = {108};
static const uint16_t g_libSceMouse_modules[1] = {109};
static const uint16_t g_libSceCompanionHttpd_modules[1] = {110};
static const uint16_t g_libSceWebBrowserDialog_modules[1] = {111};
static const uint16_t g_libSceErrorDialog_modules[1] = {112};
static const uint16_t g_libSceNpTrophy_modules[1] = {113};
static const uint16_t g_ulobjmgr_modules[1] = {114};
static const uint16_t g_libSceVideoCoreInterface_modules[1] = {115};
static const uint16_t g_libSceVideoCoreServerInterface_modules[1] = {116};
static const uint16_t g_libSceNpSns_modules[1] = {117};
static const uint16_t g_libSceNpSnsFacebookDialog_modules[2] = {118, 117};
static const uint16_t g_libSceMoveTracker_modules[1] = {119};
static const uint16_t g_libSceNpProfileDialog_modules[1] = {120};
static const uint16_t g_libSceNpFriendListDialog_modules[1] = {121};
static const uint16_t g_libSceAppContent_modules[1] = {122};
static const uint16_t g_libSceMarlin_modules[1] = {123};
static const uint16_t g_libSceDtsEnc_modules[2] = {124, 16};
static const uint16_t g_libSceNpSignaling_modules[1] = {125};
static const uint16_t g_libSceRemoteplay_modules[1] = {126};
static const uint16_t g_libSceUsbd_modules[1] = {127};
static const uint16_t g_libSceGameCustomDataDialog_modules[1] = {128};
static const uint16_t g_libSceNpEulaDialog_modules[1] = {129};
static const uint16_t g_libSceRandom_modules[1] = {130};
static const uint16_t g_libSceDipsw_modules[1] = {131};
static const uint16_t g_libSceS3DConversion_modules[1] = {132};
static const uint16_t g_libSceOttvCapture_debug_modules[1] = {133};
static const uint16_t g_libSceBgft_modules[1] = {134};
static const uint16_t g_libSceAudiodecCpuDdp_modules[1] = {135};
static const uint16_t g_libSceAudiodecCpuM4aac_modules[1] = {136};
static const uint16_t g_libSceAudiodecCpuDts_modules[1] = {137};
static const uint16_t g_libSceAudiodecCpuDtsHdLbr_modules[1] = {138};
static const uint16_t g_libSceAudiodecCpuDtsHdMa_modules[1] = {139};
static const uint16_t g_libSceAudiodecCpuLpcm_modules[1] = {140};
static const uint16_t g_libSceBemp2sys_modules[1] = {141};
static const uint16_t g_libSceBeisobmf_modules[1] = {142};
static const uint16_t g_libScePlayReady_modules[1] = {143};
static const uint16_t g_libSceVideoNativeExtEssential_modules[1] = {144};
static const uint16_t g_libSceZlib_modules[1] = {145};
static const uint16_t g_libSceIduUtil_modules[1] = {146};
static const uint16_t g_libScePsm_modules[1] = {147};
static const uint16_t g_libSceDtcpIp_modules[1] = {148};
static const uint16_t g_libSceKbEmulate_modules[1] = {149};
static const uint16_t g_libSceAppChecker_modules[1] = {150};
static const uint16_t g_libSceNpGriefReport_modules[1] = {151};
static const uint16_t g_libSceContentSearch_modules[1] = {152};
static const uint16_t g_libSceShareUtility_modules[1] = {153};
static const uint16_t g_libSceWeb_modules[6] = {154, 155, 147, 192, 27, 6};
static const uint16_t g_libSceWebKit2_modules[30] = {155, 266, 90, 6, 8, 255, 192, 116, 266, 90, 6,  8, 12, 11, 7,
                                   17,  7,   26, 8, 6, 257, 130, 39,  9,   5,  19, 6, 5,  8,  9};
static const uint16_t g_libSceDeci4h_debug_modules[1] = {156};
static const uint16_t g_libSceHeadTracker_modules[1] = {157};
static const uint16_t g_libSceGameUpdate_modules[2] = {158, 6};
static const uint16_t g_libSceAutoMounterClient_modules[2] = {159, 6};
static const uint16_t g_libSceSystemGesture_modules[1] = {160};
static const uint16_t g_libSceVdecSavc_modules[1] = {161};
static const uint16_t g_libSceVdecSavc2_modules[1] = {162};
static const uint16_t g_libSceVideodec2_modules[3] = {163, 80, 162};
static const uint16_t g_libSceVdecwrap_modules[2] = {164, 80};
static const uint16_t g_libSceVshctl_modules[1] = {165};
static const uint16_t g_libSceAt9Enc_modules[1] = {166};
static const uint16_t g_libSceConvertKeycode_modules[1] = {167};
static const uint16_t g_libSceGpuException_modules[1] = {168};
static const uint16_t g_libSceSharePlay_modules[1] = {169};
static const uint16_t g_libSceAudiodReport_modules[1] = {170};
static const uint16_t g_libSceSulphaDrv_modules[1] = {171};
static const uint16_t g_libSceHmd_modules[1] = {172};
static const uint16_t g_libSceUsbStorage_modules[2] = {173, 6};
static const uint16_t g_libSceVdecShevc_modules[1] = {174};
static const uint16_t g_libSceUsbStorageDialog_modules[1] = {175};
static const uint16_t g_libSceFaceTracker_modules[2] = {176, 59};
static const uint16_t g_libSceHandTracker_modules[1] = {177};
static const uint16_t g_libSceNpSnsYouTubeDialog_modules[2] = {178, 117};
static const uint16_t g_libSceVrTracker_modules[6] = {179, 6, 172, 31, 17, 73};
static const uint16_t g_libSceProfileCacheExternal_modules[2] = {180, 6};
static const uint16_t g_libSceBackupRestoreUtil_modules[1] = {181};
static const uint16_t g_libSceMusicPlayerService_modules[2] = {182, 183};
static const uint16_t g_libSceMusicCoreServerClientJsEx_modules[1] = {183};
static const uint16_t g_libSceSpSysCallWrapper_modules[3] = {184, 19, 6};
static const uint16_t g_libScePs2EmuMenuDialog_modules[1] = {185};
static const uint16_t g_libSceNpSnsDailyMotionDialog_modules[1] = {186};
static const uint16_t g_libSceAudiodecCpuHevag_modules[1] = {187};
static const uint16_t g_libSceLoginDialog_modules[2] = {188, 6};
static const uint16_t g_libSceLoginService_modules[2] = {189, 6};
static const uint16_t g_libSceSigninDialog_modules[2] = {190, 6};
static const uint16_t g_libSceVdecsw_modules[3] = {191, 80, 162};
static const uint16_t g_libSceOrbisCompat_modules[24] = {192, 116, 266, 90,  6,  8, 12, 11, 7, 17, 7, 26,
                                       8,   6,   257, 130, 39, 9, 5,  19, 6, 5,  8, 9};
static const uint16_t g_libSceCoreIPC_modules[1] = {193};
static const uint16_t g_libSceCustomMusicCore_modules[12] = {194, 29, 8, 27, 6, 14, 11, 7, 6, 11, 7, 6};
static const uint16_t g_libSceJson2_modules[1] = {195};
static const uint16_t g_libSceAudioLatencyEstimation_modules[1] = {196};
static const uint16_t g_libSceWkFontConfig_modules[1] = {197};
static const uint16_t g_libSceVorbisDec_modules[3] = {198, 67, 16};
static const uint16_t g_libSceTtsCoreEnUs_modules[1] = {199};
static const uint16_t g_libSceTtsCoreJp_modules[1] = {200};
static const uint16_t g_libSceOpusCeltEnc_modules[2] = {201, 16};
static const uint16_t g_libSceOpusCeltDec_modules[2] = {202, 16};
static const uint16_t g_libSceLoginMgrServer_modules[1] = {203};
static const uint16_t g_libSceHmdSetupDialog_modules[1] = {204};
static const uint16_t g_libSceVideoOutSecondary_modules[6] = {205, 82, 6, 12, 11, 7};
static const uint16_t g_libSceContentDelete_modules[1] = {206};
static const uint16_t g_libSceImeBackend_modules[1] = {207};
static const uint16_t g_libSceNetCtlApDialog_modules[1] = {208};
static const uint16_t g_libSceGnmResourceRegistration_modules[1] = {209};
static const uint16_t g_libScePlayGoDialog_modules[1] = {210};
static const uint16_t g_libSceSocialScreen_modules[7] = {211, 205, 82, 6, 12, 11, 7};
static const uint16_t g_libSceEditMp4_modules[1] = {212};
static const uint16_t g_libScePsmKitSystem_modules[1] = {221};
static const uint16_t g_libSceTextToSpeech_modules[1] = {222};
static const uint16_t g_libSceNpToolkit_modules[1] = {223};
static const uint16_t g_libSceCustomMusicService_modules[2] = {224, 183};
static const uint16_t g_libSceClSysCallWrapper_modules[11] = {225, 20, 39, 9, 19, 5, 39, 9, 5, 67, 16};
static const uint16_t g_libSceScm_modules[1] = {226};
static const uint16_t g_libSceSystemLogger_modules[2] = {227, 6};
static const uint16_t g_libSceBluetoothHid_modules[1] = {228};
static const uint16_t g_libSceAvPlayerStreaming_modules[1] = {229};
static const uint16_t g_libSceAudiodecCpuAlac_modules[1] = {230};
static const uint16_t g_libSceVideoDecoderArbitration_modules[1] = {231};
static const uint16_t g_libSceVrServiceDialog_modules[1] = {232};
static const uint16_t g_libSceJobManager_modules[2] = {233, 114};
static const uint16_t g_libSceAudiodecCpuFlac_modules[1] = {234};
static const uint16_t g_libSceSrcUtl_modules[2] = {235, 16};
static const uint16_t g_libSceS3da_modules[1] = {236};
static const uint16_t g_libSceDseehx_modules[1] = {237};
static const uint16_t g_libSceShareFactoryUtil_modules[1] = {238};
static const uint16_t g_libSceDataTransfer_modules[1] = {239};
static const uint16_t g_libSceSocialScreenDialog_modules[1] = {240};
static const uint16_t g_libSceAbstractStorage_modules[1] = {241};
static const uint16_t g_libSceImageUtil_modules[1] = {242};
static const uint16_t g_libSceMetadataReaderWriter_modules[1] = {243};
static const uint16_t g_libSceJpegParser_modules[1] = {244};
static const uint16_t g_libSceGvMp4Parser_modules[1] = {245};
static const uint16_t g_libScePngParser_modules[1] = {246};
static const uint16_t g_libSceGifParser_modules[1] = {247};
static const uint16_t g_libSceNpSnsDialog_modules[2] = {248, 117};
static const uint16_t g_libSceAbstractLocal_modules[1] = {249};
static const uint16_t g_libSceAbstractFacebook_modules[1] = {250};
static const uint16_t g_libSceAbstractYoutube_modules[1] = {251};
static const uint16_t g_libSceAbstractTwitter_modules[1] = {252};
static const uint16_t g_libSceAbstractDailymotion_modules[1] = {253};
static const uint16_t g_libSceNpToolkit2_modules[1] = {254};
static const uint16_t g_libScePrecompiledShaders_modules[1] = {255};
static const uint16_t g_libSceDiscId_modules[1] = {256};
static const uint16_t g_libSceLibreSsl_modules[2] = {257, 130};
static const uint16_t g_libSceFsInternalForVsh_modules[1] = {258};
static const uint16_t g_libSceNpUniversalDataSystem_modules[1] = {259};
static const uint16_t g_libSceDolbyVision_modules[1] = {260};
static const uint16_t g_libSceOpusSilkEnc_modules[2] = {261, 16};
static const uint16_t g_libSceOpusDec_modules[2] = {262, 16};
static const uint16_t g_libSceWebKit2Secure_modules[34] = {263, 265, 26, 8, 6,  266, 90, 6,  8, 255, 192, 116,
                                         266, 90,  6,  8, 12, 11,  7,  17, 7, 26,  8,   6,
                                         257, 130, 39, 9, 5,  19,  6,  5,  8, 9};
static const uint16_t g_libSceJscCompiler_modules[1] = {264};
static const uint16_t g_libSceJitBridge_modules[4] = {265, 26, 8, 6};
static const uint16_t g_libScePigletv2VSH_modules[4] = {266, 90, 6, 8};
static const uint16_t g_libSceJitBridge_common_ex_modules[4] = {267, 26, 8, 6};
static const uint16_t g_libSceJscCompiler_common_ex_modules[1] = {268};
static const uint16_t g_libSceOrbisCompat_common_ex_modules[24] = {269, 116, 266, 90,  6,  8, 12, 11, 7, 17, 7, 26,
                                                 8,   6,   257, 130, 39, 9, 5,  19, 6, 5,  8, 9};
static const uint16_t g_libSceWeb_common_ex_modules[6] = {270, 271, 147, 269, 27, 6};
static const uint16_t g_libSceWebKit2_common_ex_modules[30] = {271, 266, 90, 6,  8, 273, 269, 116, 266, 90,
                                             6,   8,   12, 11, 7, 17,  7,   26,  8,   6,
                                             257, 130, 39, 9,  5, 19,  6,   5,   8,   9};
static const uint16_t g_libSceWebKit2Secure_common_ex_modules[34] = {
    272, 267, 26, 8, 6,  266, 90, 6,   8,   273, 269, 116, 266, 90, 6, 8, 12,
    11,  7,   17, 7, 26, 8,   6,  257, 130, 39,  9,   5,   19,  6,  5, 8, 9};
static const uint16_t g_libScePrecompiledShaders_common_ex_modules[1] = {273};
static const uint16_t g_libSceGic_modules[1] = {274};
static const uint16_t g_libSceRnpsAppMgr_modules[1] = {275};
static const uint16_t g_libSceAsyncStorageInternal_modules[1] = {276};
static const uint16_t g_libSceHttpCache_modules[1] = {277};
static const uint16_t g_libScePlayReady2_modules[1] = {278};
static const uint16_t g_libSceHdrScopes_debug_modules[1] = {279};
static const uint16_t g_libSceNKWeb_modules[1] = {280};
static const uint16_t g_libSceNKWebKit_modules[2] = {281, 282};
static const uint16_t g_libSceNKWebKitRequirements_modules[1] = {282};
static const uint16_t g_libSceVnaInternal_modules[1] = {283};
static const uint16_t g_libSceVnaWebsocket_modules[1] = {284};
static const uint16_t g_libSceCesCs_modules[1] = {285};
static const uint16_t g_libSceComposite_modules[1] = {286};
static const uint16_t g_libSceCompositeExt_modules[1] = {287};
static const uint16_t g_libSceHubAppUtil_modules[1] = {288};
static const uint16_t g_libScePosixForWebKit_modules[1] = {289};
static const uint16_t g_libSceNpPartner001_modules[1] = {290};
static const uint16_t g_libSceNpSessionSignaling_modules[75] = {
    291, 41, 23, 22, 20, 39, 19, 9,  5,  22, 20, 39, 19, 9, 8, 6,  5,  40, 20, 39, 9, 19, 5, 39, 9,
    5,   9,  19, 6,  5,  20, 39, 9,  19, 5,  39, 9,  5,  9, 5, 22, 20, 39, 19, 9,  8, 6,  5, 23, 22,
    20,  39, 19, 9,  5,  40, 20, 39, 9,  19, 5,  39, 9,  5, 9, 19, 6,  5,  39, 9,  5, 19, 6, 5,  9};
static const uint16_t g_libScePlayerInvitationDialog_modules[1] = {292};
static const uint16_t g_libSceNpCppWebApi_modules[42] = {293, 195, 41, 23, 22, 20, 39, 19, 9,  5, 22, 20, 39, 19,
                                       9,   8,   6,  5,  40, 20, 39, 9,  19, 5, 39, 9,  5,  9,
                                       19,  6,   5,  20, 39, 9,  19, 5,  39, 9, 5,  9,  5,  9};
static const uint16_t g_libSceNpEntitlementAccess_modules[1] = {294};
static const uint16_t g_libSceNpRemotePlaySessionSignaling_modules[76] = {
    295, 291, 41, 23, 22, 20, 39, 19, 9, 5,  22, 20, 39, 19, 9,  8,  6, 5,  40,
    20,  39,  9,  19, 5,  39, 9,  5,  9, 19, 6,  5,  20, 39, 9,  19, 5, 39, 9,
    5,   9,   5,  22, 20, 39, 19, 9,  8, 6,  5,  23, 22, 20, 39, 19, 9, 5,  40,
    20,  39,  9,  19, 5,  39, 9,  5,  9, 19, 6,  5,  39, 9,  5,  19, 6, 5,  9};
static const uint16_t g_libSceLibreSsl3_modules[2] = {296, 130};
static const uint16_t g_libcurl_modules[2] = {297, 289};
static const uint16_t g_libicu_modules[2] = {298, 289};
static const uint16_t g_libcairo_modules[9] = {299, 300, 301, 302, 303, 289, 298, 289, 289};
static const uint16_t g_libfontconfig_modules[1] = {300};
static const uint16_t g_libfreetype_modules[1] = {301};
static const uint16_t g_libharfbuzz_modules[1] = {302};
static const uint16_t g_libpng16_modules[2] = {303, 289};
static const uint16_t g_libSceFontGs_modules[1] = {304};
static const uint16_t g_libSceGLSlimClientVSH_modules[1] = {305};
static const uint16_t g_libSceGLSlimServerVSH_modules[1] = {306};
static const uint16_t g_libSceFontGsm_modules[1] = {307};
static const uint16_t g_libSceNpPartnerSubscription_modules[1] = {308};
static const uint16_t g_libSceNpAuthAuthorizedAppDialog_modules[1] = {309};

#define SYSMODULE_NUM_MODULES 310
static SysmoduleEntry g_modules_array[SYSMODULE_NUM_MODULES] = {
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 1, "libkernel", NULL, 0},
    {0x0, -1, 0, 1, "libSceLibcInternal", NULL, 0},
    {0x0, -1, 0, 4, "libSceFios2", NULL, 0},
    {0x0, -1, 0, 4, "libc", NULL, 0},
    {0x8000001c, -1, 0, 1, "libSceNet", g_libSceNet_modules, 1},
    {0x8000001d, -1, 0, 1, "libSceIpmi", g_libSceIpmi_modules, 1},
    {0x8000001e, -1, 0, 1, "libSceMbus", g_libSceMbus_modules, 2},
    {0x8000001f, -1, 0, 1, "libSceRegMgr", g_libSceRegMgr_modules, 1},
    {0x80000020, -1, 0, 1, "libSceRtc", g_libSceRtc_modules, 1},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x80000021, -1, 0, 1, "libSceAvSetting", g_libSceAvSetting_modules, 3},
    {0x80000022, -1, 0, 1, "libSceVideoOut", g_libSceVideoOut_modules, 3},
    {0x80000052, -1, 0, 1025, "libSceGnmDriver", g_libSceGnmDriver_modules, 4},
    {0x80000001, -1, 0, 1, "libSceAudioOut", g_libSceAudioOut_modules, 4},
    {0x80000002, -1, 0, 1, "libSceAudioIn", g_libSceAudioIn_modules, 4},
    {0x80000023, -1, 0, 1, "libSceAjm", g_libSceAjm_modules, 1},
    {0x80000024, -1, 0, 1, "libScePad", g_libScePad_modules, 2},
    {0x80000025, -1, 0, 9, "libSceDbg", g_libSceDbg_debug_modules, 1},
    {0x80000009, -1, 0, 1, "libSceNetCtl", g_libSceNetCtl_modules, 2},
    {0x8000000a, -1, 0, 1, "libSceHttp", g_libSceHttp_modules, 5},
    {0x0, -1, 0, 1, "libSceSsl", g_libSceSsl_modules, 3},
    {0x8000000c, -1, 0, 1, "libSceNpCommon", g_libSceNpCommon_modules, 8},
    {0x8000000d, -1, 0, 1, "libSceNpManager", g_libSceNpManager_modules, 7},
    {0x8000000e, -1, 0, 1, "libSceNpWebApi", g_libSceNpWebApi_modules, 7},
    {0x8000000f, -1, 0, 1, "libSceSaveData", g_libSceSaveData_modules, 4},
    {0x80000010, -1, 0, 1, "libSceSystemService", g_libSceSystemService_modules, 3},
    {0x80000011, -1, 0, 1, "libSceUserService", g_libSceUserService_modules, 2},
    {0x80000018, -1, 0, 1, "libSceCommonDialog", g_libSceCommonDialog_modules, 1},
    {0x80000026, -1, 0, 1, "libSceSysUtil", g_libSceSysUtil_modules, 2},
    {0x80000019, -1, 0, 9, "libScePerf", g_libScePerf_debug_modules, 3},
    {0x8000001a, -1, 0, 1, "libSceCamera", g_libSceCamera_modules, 2},
    {0x0, -1, 0, 1, "libSceWebKit2ForVideoService", NULL, 0},
    {0x0, -1, 0, 1, "libSceOrbisCompatForVideoService", NULL, 0},
    {0xd7, -1, 0, 1, "libSceDiscMap", g_libSceDiscMap_modules, 1},
    {0x8000003d, -1, 0, 129, "libSceDbgAssist", g_libSceDbgAssist_modules, 1},
    {0x80000048, -1, 0, 9, "libSceMat", g_libSceMat_debug_modules, 1},
    {0x0, -1, 0, 1, "libSceRazorCpu", g_libSceRazorCpu_modules, 1},
    {0x80000075, -1, 0, 9, "libSceRazorCpu_debug", g_libSceRazorCpu_debug_debug_modules, 2},
    {0x8000000b, -1, 0, 1, "libSceSsl2", g_libSceSsl2_modules, 3},
    {0x8000008c, -1, 0, 1, "libSceHttp2", g_libSceHttp2_modules, 13},
    {0x8000008f, -1, 0, 1, "libSceNpWebApi2", g_libSceNpWebApi2_modules, 39},
    {0x8000008d, -1, 0, 1, "libSceNpGameIntent", g_libSceNpGameIntent_modules, 10},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x6, -1, 0, 1, "libSceFiber", g_libSceFiber_modules, 5},
    {0x7, -1, 0, 1, "libSceUlt", g_libSceUlt_modules, 6},
    {0xb, -1, 0, 1, "libSceNgs2", g_libSceNgs2_modules, 2},
    {0x17, -1, 0, 1, "libSceXml", g_libSceXml_modules, 1},
    {0x19, -1, 0, 1, "libSceNpUtility", g_libSceNpUtility_modules, 5},
    {0x1a, -1, 0, 1, "libSceVoice", g_libSceVoice_modules, 4},
    {0x1c, -1, 0, 1, "libSceNpMatching2", g_libSceNpMatching2_modules, 7},
    {0x1e, -1, 0, 1, "libSceNpScoreRanking", g_libSceNpScoreRanking_modules, 3},
    {0x21, -1, 0, 1, "libSceRudp", g_libSceRudp_modules, 1},
    {0x2c, -1, 0, 1, "libSceNpTus", g_libSceNpTus_modules, 3},
    {0x38, -1, 0, 4, "libSceFace", g_libSceFace_modules, 1},
    {0x39, -1, 0, 4, "libSceSmart", g_libSceSmart_modules, 1},
    {0x80, -1, 0, 1, "libSceJson", g_libSceJson_modules, 1},
    {0x81, -1, 0, 1, "libSceGameLiveStreaming", g_libSceGameLiveStreaming_modules, 2},
    {0x82, -1, 0, 1, "libSceCompanionUtil", g_libSceCompanionUtil_modules, 3},
    {0x83, -1, 0, 1, "libScePlayGo", g_libScePlayGo_modules, 1},
    {0x84, -1, 0, 1, "libSceFont", g_libSceFont_modules, 1},
    {0x85, -1, 0, 1, "libSceVideoRecording", g_libSceVideoRecording_modules, 2},
    {0x88, -1, 0, 1, "libSceAudiodec", g_libSceAudiodec_modules, 2},
    {0x8a, -1, 0, 1, "libSceJpegDec", g_libSceJpegDec_modules, 1},
    {0x8b, -1, 0, 1, "libSceJpegEnc", g_libSceJpegEnc_modules, 1},
    {0x8c, -1, 0, 1, "libScePngDec", g_libScePngDec_modules, 1},
    {0x8d, -1, 0, 1, "libScePngEnc", g_libScePngEnc_modules, 1},
    {0x8e, -1, 0, 2049, "libSceVideodec", g_libSceVideodec_modules, 3},
    {0x8f, -1, 0, 1, "libSceMove", g_libSceMove_modules, 1},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x91, -1, 0, 1, "libScePadTracker", g_libScePadTracker_modules, 2},
    {0x92, -1, 0, 1, "libSceDepth", g_libSceDepth_modules, 2},
    {0x93, -1, 0, 4, "libSceHand", g_libSceHand_modules, 1},
    {0x95, -1, 0, 1, "libSceIme", g_libSceIme_modules, 2},
    {0x96, -1, 0, 1, "libSceImeDialog", g_libSceImeDialog_modules, 2},
    {0x80000015, -1, 0, 1, "libSceVdecCore", g_libSceVdecCore_modules, 1},
    {0x97, -1, 0, 1, "libSceNpParty", g_libSceNpParty_modules, 2},
    {0x80000003, -1, 0, 1, "libSceAvcap", g_libSceAvcap_modules, 2},
    {0x98, -1, 0, 1, "libSceFontFt", g_libSceFontFt_modules, 1},
    {0x99, -1, 0, 1, "libSceFreeTypeOt", g_libSceFreeTypeOt_modules, 1},
    {0x9a, -1, 0, 1, "libSceFreeTypeOl", g_libSceFreeTypeOl_modules, 1},
    {0x9b, -1, 0, 1, "libSceFreeTypeOptOl", g_libSceFreeTypeOptOl_modules, 1},
    {0x9c, -1, 0, 1, "libSceScreenShot", g_libSceScreenShot_modules, 3},
    {0x9d, -1, 0, 1, "libSceNpAuth", g_libSceNpAuth_modules, 3},
    {0x1b, -1, 0, 1, "libSceVoiceQos", g_libSceVoiceQos_modules, 5},
    {0x80000004, -1, 0, 1, "libSceSysCore", g_libSceSysCore_modules, 2},
    {0xbc, -1, 0, 1, "libSceM4aacEnc", g_libSceM4aacEnc_modules, 2},
    {0xbd, -1, 0, 1, "libSceAudiodecCpu", g_libSceAudiodecCpu_modules, 1},
    {0x80000007, -1, 0, 1, "libSceCdlgUtilServer", g_libSceCdlgUtilServer_modules, 2},
    {0x9f, -1, 0, 9, "libSceSulpha", g_libSceSulpha_debug_modules, 1},
    {0xa0, -1, 0, 1, "libSceSaveDataDialog", g_libSceSaveDataDialog_modules, 4},
    {0xa2, -1, 0, 1, "libSceInvitationDialog", g_libSceInvitationDialog_modules, 1},
    {0xa3, -1, 0, 2057, "libSceKeyboard", g_libSceKeyboard_debug_modules, 1},
    {0x106, -1, 0, 2049, "libSceKeyboard", g_libSceKeyboard_modules, 1},
    {0xa4, -1, 0, 1, "libSceMsgDialog", g_libSceMsgDialog_modules, 1},
    {0xa5, -1, 0, 1, "libSceAvPlayer", g_libSceAvPlayer_modules, 1},
    {0xa6, -1, 0, 1, "libSceContentExport", g_libSceContentExport_modules, 1},
    {0x80000012, -1, 0, 2, "libSceVisionManager", g_libSceVisionManager_modules, 1},
    {0x80000013, -1, 0, 2, "libSceAc3Enc", g_libSceAc3Enc_modules, 2},
    {0x80000014, -1, 0, 1, "libSceAppInstUtil", g_libSceAppInstUtil_modules, 1},
    {0x80000016, -1, 0, 514, "libSceVencCore", g_libSceVencCore_modules, 1},
    {0xa7, -1, 0, 1, "libSceAudio3d", g_libSceAudio3d_modules, 1},
    {0xa8, -1, 0, 1, "libSceNpCommerce", g_libSceNpCommerce_modules, 1},
    {0x80000017, -1, 0, 1, "libSceHidControl", g_libSceHidControl_modules, 1},
    {0xa9, -1, 0, 1, "libSceMouse", g_libSceMouse_modules, 1},
    {0xaa, -1, 0, 1, "libSceCompanionHttpd", g_libSceCompanionHttpd_modules, 1},
    {0xab, -1, 0, 1, "libSceWebBrowserDialog", g_libSceWebBrowserDialog_modules, 1},
    {0xac, -1, 0, 1, "libSceErrorDialog", g_libSceErrorDialog_modules, 1},
    {0xad, -1, 0, 1, "libSceNpTrophy", g_libSceNpTrophy_modules, 1},
    {0x0, -1, 0, 1, "ulobjmgr", g_ulobjmgr_modules, 1},
    {0xae, -1, 0, 1, "libSceVideoCoreInterface", g_libSceVideoCoreInterface_modules, 1},
    {0xaf, -1, 0, 1, "libSceVideoCoreServerInterface", g_libSceVideoCoreServerInterface_modules,
      1},
    {0x8000001b, -1, 0, 1, "libSceNpSns", g_libSceNpSns_modules, 1},
    {0xb0, -1, 0, 1, "libSceNpSnsFacebookDialog", g_libSceNpSnsFacebookDialog_modules, 2},
    {0xb1, -1, 0, 1, "libSceMoveTracker", g_libSceMoveTracker_modules, 1},
    {0xb2, -1, 0, 1, "libSceNpProfileDialog", g_libSceNpProfileDialog_modules, 1},
    {0xb3, -1, 0, 1, "libSceNpFriendListDialog", g_libSceNpFriendListDialog_modules, 1},
    {0xb4, -1, 0, 1, "libSceAppContent", g_libSceAppContent_modules, 1},
    {0x80000027, -1, 0, 2, "libSceMarlin", g_libSceMarlin_modules, 1},
    {0x80000028, -1, 0, 2, "libSceDtsEnc", g_libSceDtsEnc_modules, 2},
    {0xb5, -1, 0, 1, "libSceNpSignaling", g_libSceNpSignaling_modules, 1},
    {0xb6, -1, 0, 1, "libSceRemoteplay", g_libSceRemoteplay_modules, 1},
    {0xb7, -1, 0, 1, "libSceUsbd", g_libSceUsbd_modules, 1},
    {0xb8, -1, 0, 1, "libSceGameCustomDataDialog", g_libSceGameCustomDataDialog_modules, 1},
    {0xb9, -1, 0, 1, "libSceNpEulaDialog", g_libSceNpEulaDialog_modules, 1},
    {0xba, -1, 0, 1, "libSceRandom", g_libSceRandom_modules, 1},
    {0x80000029, -1, 0, 2, "libSceDipsw", g_libSceDipsw_modules, 1},
    {0x86, -1, 0, 4, "libSceS3DConversion", g_libSceS3DConversion_modules, 1},
    {0x8000003e, -1, 0, 9, "libSceOttvCapture", g_libSceOttvCapture_debug_modules, 1},
    {0x8000002a, -1, 0, 1, "libSceBgft", g_libSceBgft_modules, 1},
    {0xbe, -1, 0, 1, "libSceAudiodecCpuDdp", g_libSceAudiodecCpuDdp_modules, 1},
    {0xc0, -1, 0, 1, "libSceAudiodecCpuM4aac", g_libSceAudiodecCpuM4aac_modules, 1},
    {0x8000002b, -1, 0, 2, "libSceAudiodecCpuDts", g_libSceAudiodecCpuDts_modules, 1},
    {0xc9, -1, 0, 1, "libSceAudiodecCpuDtsHdLbr", g_libSceAudiodecCpuDtsHdLbr_modules, 1},
    {0x8000002d, -1, 0, 2, "libSceAudiodecCpuDtsHdMa", g_libSceAudiodecCpuDtsHdMa_modules, 1},
    {0x8000002e, -1, 0, 2, "libSceAudiodecCpuLpcm", g_libSceAudiodecCpuLpcm_modules, 1},
    {0xc1, -1, 0, 1, "libSceBemp2sys", g_libSceBemp2sys_modules, 1},
    {0xc2, -1, 0, 1, "libSceBeisobmf", g_libSceBeisobmf_modules, 1},
    {0xc3, -1, 0, 1, "libScePlayReady", g_libScePlayReady_modules, 1},
    {0xc4, -1, 0, 1, "libSceVideoNativeExtEssential", g_libSceVideoNativeExtEssential_modules, 1},
    {0xc5, -1, 0, 1, "libSceZlib", g_libSceZlib_modules, 1},
    {0x8000002f, -1, 0, 1, "libSceIduUtil", g_libSceIduUtil_modules, 1},
    {0x80000030, -1, 0, 1, "libScePsm", g_libScePsm_modules, 1},
    {0xc6, -1, 0, 1, "libSceDtcpIp", g_libSceDtcpIp_modules, 1},
    {0x80000031, -1, 0, 1, "libSceKbEmulate", g_libSceKbEmulate_modules, 1},
    {0x80000032, -1, 0, 2, "libSceAppChecker", g_libSceAppChecker_modules, 1},
    {0x80000033, -1, 0, 1, "libSceNpGriefReport", g_libSceNpGriefReport_modules, 1},
    {0xc7, -1, 0, 1, "libSceContentSearch", g_libSceContentSearch_modules, 1},
    {0xc8, -1, 0, 1, "libSceShareUtility", g_libSceShareUtility_modules, 1},
    {0x80000034, -1, 0, 1, "libSceWeb", g_libSceWeb_modules, 6},
    {0x8000006a, -1, 0, 1, "libSceWebKit2", g_libSceWebKit2_modules, 30},
    {0xca, -1, 0, 9, "libSceDeci4h", g_libSceDeci4h_debug_modules, 1},
    {0xcb, -1, 0, 4, "libSceHeadTracker", g_libSceHeadTracker_modules, 1},
    {0xcc, -1, 0, 1, "libSceGameUpdate", g_libSceGameUpdate_modules, 2},
    {0xcd, -1, 0, 1, "libSceAutoMounterClient", g_libSceAutoMounterClient_modules, 2},
    {0xce, -1, 0, 1, "libSceSystemGesture", g_libSceSystemGesture_modules, 1},
    {0x80000035, -1, 0, 1, "libSceVdecSavc", g_libSceVdecSavc_modules, 1},
    {0x80000036, -1, 0, 1, "libSceVdecSavc2", g_libSceVdecSavc2_modules, 1},
    {0xcf, -1, 0, 2049, "libSceVideodec2", g_libSceVideodec2_modules, 3},
    {0xd0, -1, 0, 1, "libSceVdecwrap", g_libSceVdecwrap_modules, 2},
    {0x80000037, -1, 0, 1, "libSceVshctl", g_libSceVshctl_modules, 1},
    {0xd1, -1, 0, 1, "libSceAt9Enc", g_libSceAt9Enc_modules, 1},
    {0xd2, -1, 0, 1, "libSceConvertKeycode", g_libSceConvertKeycode_modules, 1},
    {0x80000039, -1, 0, 1, "libSceGpuException", g_libSceGpuException_modules, 1},
    {0xd3, -1, 0, 1, "libSceSharePlay", g_libSceSharePlay_modules, 1},
    {0x8000003a, -1, 0, 2, "libSceAudiodReport", g_libSceAudiodReport_modules, 1},
    {0x8000003b, -1, 0, 2, "libSceSulphaDrv", g_libSceSulphaDrv_modules, 1},
    {0xd4, -1, 0, 1, "libSceHmd", g_libSceHmd_modules, 1},
    {0xd5, -1, 0, 1, "libSceUsbStorage", g_libSceUsbStorage_modules, 2},
    {0x8000003c, -1, 0, 1, "libSceVdecShevc", g_libSceVdecShevc_modules, 1},
    {0xd6, -1, 0, 1, "libSceUsbStorageDialog", g_libSceUsbStorageDialog_modules, 1},
    {0xd8, -1, 0, 4, "libSceFaceTracker", g_libSceFaceTracker_modules, 2},
    {0xd9, -1, 0, 4, "libSceHandTracker", g_libSceHandTracker_modules, 1},
    {0xda, -1, 0, 1, "libSceNpSnsYouTubeDialog", g_libSceNpSnsYouTubeDialog_modules, 2},
    {0xed, -1, 0, 1, "libSceVrTracker", g_libSceVrTracker_modules, 6},
    {0xdc, -1, 0, 1, "libSceProfileCacheExternal", g_libSceProfileCacheExternal_modules, 2},
    {0x8000003f, -1, 0, 1, "libSceBackupRestoreUtil", g_libSceBackupRestoreUtil_modules, 1},
    {0xdd, -1, 0, 1, "libSceMusicPlayerService", g_libSceMusicPlayerService_modules, 2},
    {0x0, -1, 0, 1, "libSceMusicCoreServerClientJsEx", g_libSceMusicCoreServerClientJsEx_modules,
      1},
    {0xde, -1, 0, 1, "libSceSpSysCallWrapper", g_libSceSpSysCallWrapper_modules, 3},
    {0xdf, -1, 0, 1, "libScePs2EmuMenuDialog", g_libScePs2EmuMenuDialog_modules, 1},
    {0xe0, -1, 0, 1, "libSceNpSnsDailyMotionDialog", g_libSceNpSnsDailyMotionDialog_modules, 1},
    {0xe1, -1, 0, 1, "libSceAudiodecCpuHevag", g_libSceAudiodecCpuHevag_modules, 1},
    {0xe2, -1, 0, 1, "libSceLoginDialog", g_libSceLoginDialog_modules, 2},
    {0xe3, -1, 0, 1, "libSceLoginService", g_libSceLoginService_modules, 2},
    {0xe4, -1, 0, 1, "libSceSigninDialog", g_libSceSigninDialog_modules, 2},
    {0xe5, -1, 0, 1, "libSceVdecsw", g_libSceVdecsw_modules, 3},
    {0x8000006d, -1, 0, 1, "libSceOrbisCompat", g_libSceOrbisCompat_modules, 24},
    {0x0, -1, 0, 1, "libSceCoreIPC", g_libSceCoreIPC_modules, 1},
    {0xe6, -1, 0, 1, "libSceCustomMusicCore", g_libSceCustomMusicCore_modules, 12},
    {0xe7, -1, 0, 1, "libSceJson2", g_libSceJson2_modules, 1},
    {0xe8, -1, 0, 4, "libSceAudioLatencyEstimation", g_libSceAudioLatencyEstimation_modules, 1},
    {0xe9, -1, 0, 1, "libSceWkFontConfig", g_libSceWkFontConfig_modules, 1},
    {0xea, -1, 0, 2, "libSceVorbisDec", g_libSceVorbisDec_modules, 3},
    {0x80000041, -1, 0, 1, "libSceTtsCoreEnUs", g_libSceTtsCoreEnUs_modules, 1},
    {0x80000042, -1, 0, 1, "libSceTtsCoreJp", g_libSceTtsCoreJp_modules, 1},
    {0x80000043, -1, 0, 1, "libSceOpusCeltEnc", g_libSceOpusCeltEnc_modules, 2},
    {0x80000044, -1, 0, 1, "libSceOpusCeltDec", g_libSceOpusCeltDec_modules, 2},
    {0x80000045, -1, 0, 2, "libSceLoginMgrServer", g_libSceLoginMgrServer_modules, 1},
    {0xeb, -1, 0, 1, "libSceHmdSetupDialog", g_libSceHmdSetupDialog_modules, 1},
    {0x80000046, -1, 0, 1, "libSceVideoOutSecondary", g_libSceVideoOutSecondary_modules, 6},
    {0xee, -1, 0, 1, "libSceContentDelete", g_libSceContentDelete_modules, 1},
    {0xef, -1, 0, 1, "libSceImeBackend", g_libSceImeBackend_modules, 1},
    {0xf0, -1, 0, 1, "libSceNetCtlApDialog", g_libSceNetCtlApDialog_modules, 1},
    {0x80000047, -1, 0, 1, "libSceGnmResourceRegistration",
      g_libSceGnmResourceRegistration_modules, 1},
    {0xf1, -1, 0, 1, "libScePlayGoDialog", g_libScePlayGoDialog_modules, 1},
    {0xf2, -1, 0, 1, "libSceSocialScreen", g_libSceSocialScreen_modules, 7},
    {0xf3, -1, 0, 1, "libSceEditMp4", g_libSceEditMp4_modules, 1},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0x0, -1, 0, 0, NULL, NULL, 0},
    {0xf5, -1, 0, 1, "libScePsmKitSystem", g_libScePsmKitSystem_modules, 1},
    {0xf6, -1, 0, 1, "libSceTextToSpeech", g_libSceTextToSpeech_modules, 1},
    {0xf7, -1, 0, 2052, "libSceNpToolkit", g_libSceNpToolkit_modules, 1},
    {0xf8, -1, 0, 1, "libSceCustomMusicService", g_libSceCustomMusicService_modules, 2},
    {0xf9, -1, 0, 1, "libSceClSysCallWrapper", g_libSceClSysCallWrapper_modules, 11},
    {0x80000049, -1, 0, 1, "libSceScm", g_libSceScm_modules, 1},
    {0xfa, -1, 0, 1, "libSceSystemLogger", g_libSceSystemLogger_modules, 2},
    {0xfb, -1, 0, 1, "libSceBluetoothHid", g_libSceBluetoothHid_modules, 1},
    {0x80000050, -1, 0, 1, "libSceAvPlayerStreaming", g_libSceAvPlayerStreaming_modules, 1},
    {0x80000051, -1, 0, 2, "libSceAudiodecCpuAlac", g_libSceAudiodecCpuAlac_modules, 1},
    {0xfc, -1, 0, 1, "libSceVideoDecoderArbitration", g_libSceVideoDecoderArbitration_modules, 1},
    {0xfd, -1, 0, 1, "libSceVrServiceDialog", g_libSceVrServiceDialog_modules, 1},
    {0xfe, -1, 0, 4, "libSceJobManager", g_libSceJobManager_modules, 2},
    {0x80000053, -1, 0, 2, "libSceAudiodecCpuFlac", g_libSceAudiodecCpuFlac_modules, 1},
    {0x103, -1, 0, 1, "libSceSrcUtl", g_libSceSrcUtl_modules, 2},
    {0x80000055, -1, 0, 2, "libSceS3da", g_libSceS3da_modules, 1},
    {0x80000056, -1, 0, 2, "libSceDseehx", g_libSceDseehx_modules, 1},
    {0xff, -1, 0, 1, "libSceShareFactoryUtil", g_libSceShareFactoryUtil_modules, 1},
    {0x80000057, -1, 0, 1, "libSceDataTransfer", g_libSceDataTransfer_modules, 1},
    {0x100, -1, 0, 1, "libSceSocialScreenDialog", g_libSceSocialScreenDialog_modules, 1},
    {0x80000058, -1, 0, 1, "libSceAbstractStorage", g_libSceAbstractStorage_modules, 1},
    {0x80000059, -1, 0, 1, "libSceImageUtil", g_libSceImageUtil_modules, 1},
    {0x8000005a, -1, 0, 1, "libSceMetadataReaderWriter", g_libSceMetadataReaderWriter_modules, 1},
    {0x8000005b, -1, 0, 1, "libSceJpegParser", g_libSceJpegParser_modules, 1},
    {0x8000005c, -1, 0, 1, "libSceGvMp4Parser", g_libSceGvMp4Parser_modules, 1},
    {0x8000005d, -1, 0, 1, "libScePngParser", g_libScePngParser_modules, 1},
    {0x8000005e, -1, 0, 1, "libSceGifParser", g_libSceGifParser_modules, 1},
    {0x101, -1, 0, 1, "libSceNpSnsDialog", g_libSceNpSnsDialog_modules, 2},
    {0x8000005f, -1, 0, 1, "libSceAbstractLocal", g_libSceAbstractLocal_modules, 1},
    {0x80000060, -1, 0, 1, "libSceAbstractFacebook", g_libSceAbstractFacebook_modules, 1},
    {0x80000061, -1, 0, 1, "libSceAbstractYoutube", g_libSceAbstractYoutube_modules, 1},
    {0x80000062, -1, 0, 1, "libSceAbstractTwitter", g_libSceAbstractTwitter_modules, 1},
    {0x80000063, -1, 0, 1, "libSceAbstractDailymotion", g_libSceAbstractDailymotion_modules, 1},
    {0x102, -1, 0, 2052, "libSceNpToolkit2", g_libSceNpToolkit2_modules, 1},
    {0x80000064, -1, 0, 1, "libScePrecompiledShaders", g_libScePrecompiledShaders_modules, 1},
    {0x104, -1, 0, 1, "libSceDiscId", g_libSceDiscId_modules, 1},
    {0x80000065, -1, 0, 1, "libSceLibreSsl", g_libSceLibreSsl_modules, 2},
    {0x80000066, -1, 0, 2, "libSceFsInternalForVsh", g_libSceFsInternalForVsh_modules, 1},
    {0x105, -1, 0, 1, "libSceNpUniversalDataSystem", g_libSceNpUniversalDataSystem_modules, 1},
    {0x80000067, -1, 0, 1, "libSceDolbyVision", g_libSceDolbyVision_modules, 1},
    {0x80000068, -1, 0, 1, "libSceOpusSilkEnc", g_libSceOpusSilkEnc_modules, 2},
    {0x80000069, -1, 0, 1, "libSceOpusDec", g_libSceOpusDec_modules, 2},
    {0x8000006b, -1, 0, 1, "libSceWebKit2Secure", g_libSceWebKit2Secure_modules, 34},
    {0x8000006c, -1, 0, 1, "libSceJscCompiler", g_libSceJscCompiler_modules, 1},
    {0x8000006e, -1, 0, 1, "libSceJitBridge", g_libSceJitBridge_modules, 4},
    {0x0, -1, 0, 1, "libScePigletv2VSH", g_libScePigletv2VSH_modules, 4},
    {0x8000006f, -1, 0, 4096, "libSceJitBridge", g_libSceJitBridge_common_ex_modules, 4},
    {0x80000070, -1, 0, 4096, "libSceJscCompiler", g_libSceJscCompiler_common_ex_modules, 1},
    {0x80000071, -1, 0, 4096, "libSceOrbisCompat", g_libSceOrbisCompat_common_ex_modules, 24},
    {0x80000072, -1, 0, 4096, "libSceWeb", g_libSceWeb_common_ex_modules, 6},
    {0x80000073, -1, 0, 4096, "libSceWebKit2", g_libSceWebKit2_common_ex_modules, 30},
    {0x80000074, -1, 0, 4096, "libSceWebKit2Secure", g_libSceWebKit2Secure_common_ex_modules, 34},
    {0x0, -1, 0, 4096, "libScePrecompiledShaders", g_libScePrecompiledShaders_common_ex_modules,
      1},
    {0x107, -1, 0, 1, "libSceGic", g_libSceGic_modules, 1},
    {0x80000076, -1, 0, 1, "libSceRnpsAppMgr", g_libSceRnpsAppMgr_modules, 1},
    {0x80000077, -1, 0, 1, "libSceAsyncStorageInternal", g_libSceAsyncStorageInternal_modules, 1},
    {0x80000078, -1, 0, 1, "libSceHttpCache", g_libSceHttpCache_modules, 1},
    {0x108, -1, 0, 1, "libScePlayReady2", g_libScePlayReady2_modules, 1},
    {0x109, -1, 0, 9, "libSceHdrScopes", g_libSceHdrScopes_debug_modules, 1},
    {0x80000079, -1, 0, 1, "libSceNKWeb", g_libSceNKWeb_modules, 1},
    {0x8000007a, -1, 0, 1, "libSceNKWebKit", g_libSceNKWebKit_modules, 2},
    {0x0, -1, 0, 1, "libSceNKWebKitRequirements", g_libSceNKWebKitRequirements_modules, 1},
    {0x8000007c, -1, 0, 1, "libSceVnaInternal", g_libSceVnaInternal_modules, 1},
    {0x8000007d, -1, 0, 1, "libSceVnaWebsocket", g_libSceVnaWebsocket_modules, 1},
    {0x10c, -1, 0, 1, "libSceCesCs", g_libSceCesCs_modules, 1},
    {0x8000008a, -1, 0, 2, "libSceComposite", g_libSceComposite_modules, 1},
    {0x8000008b, -1, 0, 1, "libSceCompositeExt", g_libSceCompositeExt_modules, 1},
    {0x116, -1, 0, 1, "libSceHubAppUtil", g_libSceHubAppUtil_modules, 1},
    {0x80000098, -1, 0, 1, "libScePosixForWebKit", g_libScePosixForWebKit_modules, 1},
    {0x11a, -1, 0, 1, "libSceNpPartner001", g_libSceNpPartner001_modules, 1},
    {0x112, -1, 0, 1, "libSceNpSessionSignaling", g_libSceNpSessionSignaling_modules, 75},
    {0x10d, -1, 0, 1, "libScePlayerInvitationDialog", g_libScePlayerInvitationDialog_modules, 1},
    {0x115, -1, 0, 4, "libSceNpCppWebApi", g_libSceNpCppWebApi_modules, 42},
    {0x113, -1, 0, 1, "libSceNpEntitlementAccess", g_libSceNpEntitlementAccess_modules, 1},
    {0x8000009a, -1, 0, 2, "libSceNpRemotePlaySessionSignaling",
      g_libSceNpRemotePlaySessionSignaling_modules, 76},
    {0x800000b8, -1, 0, 1, "libSceLibreSsl3", g_libSceLibreSsl3_modules, 2},
    {0x800000b1, -1, 0, 1, "libcurl", g_libcurl_modules, 2},
    {0x800000aa, -1, 0, 1, "libicu", g_libicu_modules, 2},
    {0x800000ac, -1, 0, 1, "libcairo", g_libcairo_modules, 9},
    {0x0, -1, 0, 1, "libfontconfig", g_libfontconfig_modules, 1},
    {0x0, -1, 0, 1, "libfreetype", g_libfreetype_modules, 1},
    {0x0, -1, 0, 1, "libharfbuzz", g_libharfbuzz_modules, 1},
    {0x800000ab, -1, 0, 1, "libpng16", g_libpng16_modules, 2},
    {0x12f, -1, 0, 1, "libSceFontGs", g_libSceFontGs_modules, 1},
    {0x800000c0, -1, 0, 1, "libSceGLSlimClientVSH", g_libSceGLSlimClientVSH_modules, 1},
    {0x800000c1, -1, 0, 1, "libSceGLSlimServerVSH", g_libSceGLSlimServerVSH_modules, 1},
    {0x135, -1, 0, 4, "libSceFontGsm", g_libSceFontGsm_modules, 1},
    {0x138, -1, 0, 1, "libSceNpPartnerSubscription", g_libSceNpPartnerSubscription_modules, 1},
    {0x139, -1, 0, 1, "libSceNpAuthAuthorizedAppDialog", g_libSceNpAuthAuthorizedAppDialog_modules,
      1}};



#endif // PS4_SYSMODULE_TABLE_H
