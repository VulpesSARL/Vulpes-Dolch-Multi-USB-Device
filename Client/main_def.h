/*
 * main_def.h
 *
 *  Created on: 16 Jan 2026
 *      Author: Fox
 */

#ifndef MAIN_DEF_H_
#define MAIN_DEF_H_

//functions

//Generic (in main.c)
void CyFxAppErrorHandler(CyU3PReturnStatus_t apiRetStatus);
void CyFxClientUsbConnect(void);
CyBool_t CyFxClientUsbIsConnected(void);
void CyFxClientUsbDiskChanged(CyBool_t Present);       /* the disk came or went (any thread): the USB side shows or hides the disk interface */
CyBool_t CyFxClientUsbRestartPending(void);
void ClientLinkWakeHelper(void);                       /* LINKCLIENT.c */
void CyFxClientUsbRestartRun(void);                    /* helper thread: carries out a pending restart of the USB stack */
void CyFxMscApplnDebugInit();

void CyFxMscApplnResetDatapath_CDROM(void);
void CyFxMscApplnUSBEventCB_CDROM(CyU3PUsbEventType_t evtype, uint16_t evdata);
CyBool_t CyFxMscApplnLPMRqtCB_CDROM(CyU3PUsbLinkPowerMode link_mode);
void CyFxMscApplnDmaInit_CDROM(void);
void CyFxMscApplnDmaDeInit_CDROM(void);
void CyFxMscAppInitVars_CDROM(void);
void CyFxMscAppHandleMscTask_CDROM(void);
void CyFxApplicationDefine_CDROM(void);
void CyFxMscApplnDmaCb_CDROM(CyU3PDmaChannel* handle, CyU3PDmaCbType_t   evtype, CyU3PDmaCBInput_t* input);
void CyFxMscAppInitVars_CDROM(void);
void CyFxMscAppHandleMscTask_CDROM(void);
void CyFxMscAppInitVars_CDROM(void);
void MscAppThread_Entry_CDROM(uint32_t input);
CyBool_t glInPhaseError_CDROM;
CyU3PEvent glMscAppEvent_CDROM;
CyBool_t glDevConfigured_CDROM;

void CyFxMscApplnResetDatapath_STORAGE(void);
void CyFxMscApplnUSBEventCB_STORAGE(CyU3PUsbEventType_t evtype, uint16_t evdata);
CyBool_t CyFxMscApplnLPMRqtCB_STORAGE(CyU3PUsbLinkPowerMode link_mode);
void CyFxMscApplnDmaInit_STORAGE(void);
void CyFxMscApplnDmaDeInit_STORAGE(void);
void CyFxMscAppInitVars_STORAGE(void);
void CyFxMscAppHandleMscTask_STORAGE(void);
void CyFxApplicationDefine_STORAGE(void);
void CyFxMscApplnDmaCb_STORAGE(CyU3PDmaChannel* handle, CyU3PDmaCbType_t   evtype, CyU3PDmaCBInput_t* input);
void CyFxMscAppInitVars_STORAGE(void);
void CyFxMscAppHandleMscTask_STORAGE(void);
void CyFxMscAppInitVars_STORAGE(void);
void MscAppThread_Entry_STORAGE(uint32_t input);
CyBool_t glInPhaseError_STORAGE;
CyU3PEvent glMscAppEvent_STORAGE;
CyBool_t glDevConfigured_STORAGE;

/* Power management (POWER.c) - sole owner of CyU3PSysEnterSuspendMode(), not tied to CDROM or STORAGE. */
void CyFxPmApplnUSBEventCB(CyU3PUsbEventType_t evtype, uint16_t evdata);
void CyFxPmAppHandleTask(void);
void PmAppThread_Entry(uint32_t input);
CyU3PEvent glPmAppEvent;

#endif /* MAIN_DEF_H_ */
