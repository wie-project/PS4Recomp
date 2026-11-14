// Host test for the libSceGameCustomDataDialog state machine. Build and run (from the
// repository root):
//   clang -std=c11 -I pkg/runtime/core -I pkg/runtime/modules/libSceGameCustomDataDialog \
//     tests/game_custom_data_dialog_test/main.c \
//     pkg/runtime/modules/libSceGameCustomDataDialog/ps4_game_custom_data_dialog.c \
//     -o /tmp/gcdd_test && /tmp/gcdd_test

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ps4_game_custom_data_dialog.h"
#include "recomp_context.h"

// The tested functions run no guest code: an empty dispatch table makes any guest call abort.
recomp_fn_t **g_dispatch_l1[DISPATCH_LEVEL_SIZE];

int main(void) {
    uint8_t result[GCDD_RESULT_RESERVED_OFFSET + GCDD_RESULT_RESERVED_SIZE];
    memset(result, 0, sizeof(result));

    assert(ps4_game_custom_data_dialog_update_status() == GCDD_STATUS_NONE);
    assert(ps4_game_custom_data_dialog_terminate() == GCDD_ERROR_NOT_INITIALIZED);
    assert(ps4_game_custom_data_dialog_get_result(result) == GCDD_ERROR_NOT_INITIALIZED);

    assert(ps4_game_custom_data_dialog_initialize() == 0);
    assert(ps4_game_custom_data_dialog_initialize() == GCDD_ERROR_ALREADY_INITIALIZED);
    assert(ps4_game_custom_data_dialog_update_status() == GCDD_STATUS_INITIALIZED);

    // Argument checks precede the state check; an initialized dialog that never ran has no result.
    assert(ps4_game_custom_data_dialog_get_result(NULL) == GCDD_ERROR_ARG_NULL);
    result[GCDD_RESULT_RESERVED_OFFSET + 5] = 1;
    assert(ps4_game_custom_data_dialog_get_result(result) == GCDD_ERROR_PARAM_INVALID);
    result[GCDD_RESULT_RESERVED_OFFSET + 5] = 0;
    assert(ps4_game_custom_data_dialog_get_result(result) == GCDD_ERROR_NOT_FINISHED);

    assert(ps4_game_custom_data_dialog_terminate() == 0);
    assert(ps4_game_custom_data_dialog_update_status() == GCDD_STATUS_NONE);
    assert(ps4_game_custom_data_dialog_initialize() == 0);
    assert(ps4_game_custom_data_dialog_terminate() == 0);

    printf("[gcdd-test] PASSED\n");
    return 0;
}
