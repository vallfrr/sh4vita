#include <vitaGL.h>

int main(void) {
    vglInit(0x800000);
    for (;;) {
        glClearColor(0.35f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        vglSwapBuffers(GL_FALSE);
    }
}
