#pragma once
#ifndef PUBLIC_IP
#define PUBLIC_IP

extern char public_ip[64];
void public_ip_management_task(void *pvParameters);

#endif // PUBLIC_IP