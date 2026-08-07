#ifndef EVOBOT_EZQ_ADAPTER_H
#define EVOBOT_EZQ_ADAPTER_H

#include <evobot/evobot.h>

void EvoBot_EZQ_Init(void);
void EvoBot_EZQ_MapLoaded(void);
void EvoBot_EZQ_Frame(void);
void EvoBot_EZQ_PrepareBotCommands(double frame_time);
void EvoBot_EZQ_MapCleared(void);
void EvoBot_EZQ_Shutdown(void);

#endif
