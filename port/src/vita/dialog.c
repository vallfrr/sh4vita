// The two GOG MessageBoxA call sites are MB_OK startup errors. Render them as
// Vita system messages; a log line alone is not a user-visible error.
#include "rt.h"
#include "plat.h"
#include "qgl.h"
#include "cp1252.h"
#include <psp2/message_dialog.h>
#include <stdlib.h>

int plat_message_box(const char *title, const char *text, unsigned type) {
    rt_log("[message] type=%x %s : %s\n", type, title ? title : "", text ? text : "");
    if (type & 15u) {
        rt_log("[message] unsupported button set; no response assumed\n");
        return 0;
    }
    // Called only after vitaGL initialisation. Drain the render queue before
    // using the same process-global vitaGL context directly, as for uploads.
    q_sync("message-box");
    char message[2048];
    snprintf(message, sizeof message, "%s%s%s", title ? title : "",
             title && *title ? "\n\n" : "", text ? text : "");
    char utf8[6144];
    rt_cp1252_utf8(utf8, sizeof utf8, message);
    SceMsgDialogUserMessageParam user = {0};
    user.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_OK;
    user.msg = utf8;
    SceMsgDialogParam param;
    sceMsgDialogParamInit(&param);
    param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
    param.userMsgParam = &user;
    int initialized = sceMsgDialogInit(&param);
    if (initialized < 0) {
        rt_log("[message] init failed %08x\n", (unsigned)initialized);
        return 0;
    }
    unsigned frames = 0;
    int test = getenv("SH4_MESSAGE_TEST") != NULL;
    while (sceMsgDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        vglSwapBuffers(GL_TRUE); // updates the common-dialog compositor
        if (test && ++frames == 120) sceMsgDialogClose();
    }
    SceMsgDialogResult result = {0};
    int read = sceMsgDialogGetResult(&result);
    int terminated = sceMsgDialogTerm();
    rt_log("[message] result-read=%08x result=%d button=%d term=%08x frames=%u\n",
           (unsigned)read, result.result, result.buttonId, (unsigned)terminated, frames);
    // An externally aborted/failed dialogue is never an implicit OK.
    return read >= 0 && terminated >= 0 && result.result == SCE_COMMON_DIALOG_RESULT_OK &&
           result.buttonId == SCE_MSG_DIALOG_BUTTON_ID_OK ? 1 : 0;
}
