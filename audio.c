#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "config.h"

#ifdef CONFIG_AO
extern audio_output audio_ao;
#endif
extern audio_output audio_dummy;

static audio_output *outputs[] = {
#ifdef CONFIG_AO
    &audio_ao,
#endif
    &audio_dummy,
    NULL
};
    

audio_output *audio_get_output(char *name) {
    audio_output **out;
    
    // default to the first
    if (!name)
        return outputs[0];

    for (out=outputs; *out; out++)
        if (!strcasecmp(name, (*out)->name))
            return *out;

    return NULL;
}

void audio_ls_outputs(void) {
    audio_output **out;

    printf("Available audio outputs:\n");
    for (out=outputs; *out; out++)
        printf("    %s%s\n", (*out)->name, out==outputs ? " (default)" : "");
}
