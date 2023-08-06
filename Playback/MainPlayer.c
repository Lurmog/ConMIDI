#include "MIDIClock.c"

unsigned long long sentEvents = 0;
unsigned long totalFrames = 0;
double startTime1 = 0;
double startTime2 = 0;

int* PPushback;
unsigned long long* currOffset;
unsigned long int* currEvent;
unsigned long int* trackReadOffset;
BOOL* prepareStep;
BOOL* tempoEvent;
unsigned long long* trackPosition;

unsigned char PRead(const unsigned char* track, unsigned long int* trackReadOffset, int* push){
    if(*push != -1){
        int temp = *push;
        *push = -1;
        return temp;
    }
    (*trackReadOffset)++;
    return track[*trackReadOffset-1];
}
unsigned char PReadFast(const unsigned char* track, unsigned long int* trackReadOffset){
    (*trackReadOffset)++;
    return track[*trackReadOffset-1];
}

unsigned long int PReadVariableLen(const unsigned char* track, unsigned long int* trackReadOffset, int* push){
    byte temp;
    unsigned long int val = 0;
    for(int i = 0; i < 4; i++){
        temp = PReadFast(track,trackReadOffset);
        if(temp > 0x7F){
            val = (val << 7) | (temp & 0x7F);
        } else {
            val = val << 7 | temp;
            return val;
        }
    }
    return val;
}

void StartTimeCheck()
{
    double tempT = getTimeMsec();
    if((long)(tempT-startTime1)>=16){
        if((long)(tempT-startTime2)>=1000){
            printf("\nFPS: %.10g",(float)1/((float)(tempT-startTime2)/(float)1000/(float)totalFrames));
            totalFrames = 0;
            startTime2 = tempT;
        }
        startTime1 = tempT;
        char temp[256] = "";
        char num[20];
        char fpstemp[32];
        strcat(temp,prgTitle);
        strcat(temp," | Played events: ");
        sprintf(num, "%llu", sentEvents);
        strcat(temp,num);
        strcat(temp," | BPM: ");
        sprintf(fpstemp, "%.10g", bpm);
        strcat(temp,fpstemp);
        SetConsoleTitle(temp);
    }
}
byte prevEvent = 0;
BOOL prepareEvent(const unsigned char* track, BOOL* prepareStep, unsigned long long* currOffset, BOOL* tempoEvent, unsigned long int* trackReadOffset, unsigned long int* currEvent, int* push, unsigned int trackId){
    if(*prepareStep==FALSE){
        return TRUE;
    }
    *prepareStep=FALSE;
    *currOffset=0;
    *tempoEvent=FALSE;
    unsigned long int event = 0;
    while(TRUE){
        *currOffset+=PReadVariableLen(track,trackReadOffset,push);
        byte readEvent = PReadFast(track,trackReadOffset);
        if(readEvent < 0x80){
            *push = readEvent;
            readEvent = prevEvent;
        }
        prevEvent = readEvent;
        BOOL proceed = TRUE;
        byte trackEvent = readEvent & 0b11110000;
        if(readEvent>=128&&readEvent<=239){
            if(trackEvent==0b10010000||trackEvent==0b10000000){
                event = (readEvent | (PRead(track,trackReadOffset,push) << 8) |(PReadFast(track,trackReadOffset) << 16));
            } else if(trackEvent==0b10100000||trackEvent==0b11100000||trackEvent==0b10110000){
                event = (readEvent | (PRead(track,trackReadOffset,push) << 8) |(PRead(track,trackReadOffset,push) << 16));
            } else if(trackEvent==0b11000000||trackEvent==0b11010000){
                event = (readEvent | (PRead(track,trackReadOffset,push) << 8));
            } else {
                proceed = FALSE;
            }
        } else if(readEvent==0){
            break;
        } else {
            switch(readEvent){
                case 0b11110000:
                    while(PRead(track,trackReadOffset,push)!=0b11110111);
                    break;
                case 0b11110010:
                    *trackReadOffset+=2;
                    break;
                case 0b11110011:
                    *trackReadOffset+=1;
                    break;
                case 0xFF:
                    readEvent = PRead(track,trackReadOffset,push);
                    if(readEvent == 81){
                        *trackReadOffset+=1;
                        for (int i = 0; i != 3; i++){
                            byte temp = PRead(track,trackReadOffset,push);
                            event = (event<<8)|temp;
                        }
                        *tempoEvent=TRUE;
                    } else if(readEvent == 0x2F){
                        proceed = FALSE;
                        break;
                    } else {
                        *trackReadOffset+=PRead(track,trackReadOffset,push);
                    }
                    break;
            }
        }
        if(proceed==FALSE){
            return FALSE;
        }
        *currEvent=event;
        return TRUE;
    }
}

void StartPlayback(){
    double clock = 0;
    BOOL trackFinished[realTracks];
    unsigned int aliveTracks = realTracks;
    currOffset = (unsigned long long*)malloc(realTracks * sizeof(unsigned long long));
    currEvent = (unsigned long int*)malloc(realTracks * sizeof(unsigned long int));
    trackReadOffset = (unsigned long int*)malloc(realTracks * sizeof(unsigned long int));
    trackPosition = (unsigned long long*)malloc(realTracks * sizeof(unsigned long long));
    PPushback = (int*)malloc(realTracks * sizeof(int));
    prepareStep = (BOOL*)malloc(realTracks * sizeof(BOOL));
    tempoEvent = (BOOL*)malloc(realTracks * sizeof(BOOL));
    memset(currOffset,0,realTracks * sizeof(unsigned long long));
    memset(currEvent,0,realTracks * sizeof(unsigned long int));
    memset(trackPosition,0,realTracks * sizeof(unsigned long long));
    memset(trackReadOffset,0,realTracks * sizeof(unsigned long int));
    memset(PPushback,-1,realTracks * sizeof(int));
    memset(prepareStep,TRUE,realTracks * sizeof(BOOL));
    memset(tempoEvent,FALSE,realTracks * sizeof(BOOL));
    memset(trackFinished,FALSE,sizeof(trackFinished));
    startTime1 = getTimeMsec();
    startTime2 = getTimeMsec();
    cppq = ppq;
    Clock_Start();
    int (*SendDirectData)(DWORD) = SendDirectDataPtr;
    while(TRUE){
        StartTimeCheck();
        double newClock = Clock_GetTick();
        if(newClock!=clock){
            totalFrames++;
            clock=newClock;
            unsigned long long *tPos = &trackPosition[0];
            unsigned long long *cOff = &currOffset[0];
            unsigned long int *cEv = &currEvent[0];
            unsigned long int *tRO = &trackReadOffset[0];
            const unsigned char* currentTrack;
            BOOL *pStep = &prepareStep[0];
            int *pPushbacks = &PPushback[0];
            BOOL *tEv = &tempoEvent[0];
            unsigned long long clockUInt64 = (unsigned long long)clock;
            BOOL *tF1 = &trackFinished[0];
            for(unsigned int i = 0; i < realTracks; i++){
                if(*tF1==FALSE){
                    currentTrack=tracks[i];
                    while(TRUE){
                        if(prepareEvent(currentTrack,pStep,cOff,tEv,tRO,cEv,pPushbacks,i)){
                            if(*tPos+*cOff<=clockUInt64){
                                *pStep=TRUE;
                                *tPos+=*cOff;
                                switch(*tEv){
                                    case FALSE:
                                        SendDirectData(*cEv);
                                        sentEvents++;
                                        break;
                                    case TRUE:
                                        Clock_SubmitBPM(*tPos,*cEv);
                                        break;
                                }
                            } else {
                                break;
                            }
                        } else {
                            aliveTracks--;
                            *tF1 = TRUE;
                            break;
                        }
                    }
                }
                *tF1++;*tPos++;*cOff++;*pStep++;*cEv++;*tEv++;*pPushbacks++;*tRO++;
            }
        } else {
            usleep(1000);
        }
        if(aliveTracks == 0){
            printf("\nRan out of events, playback finished.");
            sleep(3);
            exit(0);
        }
    }
}
