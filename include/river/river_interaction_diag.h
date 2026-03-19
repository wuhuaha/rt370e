#ifndef AMEBA_RIVER_INTERACTION_DIAG_H
#define AMEBA_RIVER_INTERACTION_DIAG_H

#include "river/river_types.h"

river_status_t river_interaction_diag_init(void);
river_status_t river_interaction_diag_route_text(const char *text,
                                                const char *source,
                                                const char *sid);
river_status_t river_interaction_diag_submit_tts_test(const char *source);
river_status_t river_interaction_diag_invoke_intent(const char *intent_name,
                                                    const char *source);
river_status_t river_interaction_diag_execute_device(const char *device_name,
                                                     const char *action_name,
                                                     const char *source);
river_status_t river_interaction_diag_flush_deferred(void);
void river_interaction_diag_dump_status(void);
void river_interaction_diag_dump_intents(void);

#endif
