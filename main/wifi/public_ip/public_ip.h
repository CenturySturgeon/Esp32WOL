#pragma once
#ifndef PUBLIC_IP
#define PUBLIC_IP

extern char public_ip[64];
extern TaskHandle_t public_ip_task_handle;
void public_ip_management_task(void *pvParameters);

#endif // PUBLIC_IP