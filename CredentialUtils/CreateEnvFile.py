import os
from typing import List, Dict
from CredentialUtils import UserSession, CertUtils

def create_envVariables_file(router_info: tuple[str, str, tuple[int, int, int, int], tuple[int, int, int, int]],user_sessions: List[UserSession], static_ip: tuple[int, int, int, int], telegram_info: tuple[str, str, tuple[str, Dict | None]], cert_and_pkey: tuple[bytes, bytes], ip_site_info: tuple[tuple[str, Dict | None], str], path_sufix: str = ''):
    """Creates the envVariables.h file with the provided information."""
    # Get the placeHolder.h absolute file path
    module_dir = os.path.dirname(__file__)
    file_path = os.path.join(module_dir, 'placeHolder.h')

    # Read the placeHolders.h file
    with open(file_path, 'r') as file:
        content = file.read()

    wifi_name, wifi_password, gateway, subnet = router_info
    telegram_bot_token, telegram_chat_id, telegram_api_cert = telegram_info
    certificate, private_key = cert_and_pkey

    ip_site_cert_str = ip_site_info[0][0]
    # Gets the certificate's expiration date
    ip_site_posix_exp_date = CertUtils.date_to_posix(ip_site_info[0][1]['notAfter'])
    ip_site_url = ip_site_info[1]
    server_cert_posix_exp_date = CertUtils.cert_posix_expiration_date(certificate)
    telegram_posix_exp_date = CertUtils.date_to_posix(telegram_info[2][1]['notAfter'])

    print(f'Ip Site certificate expiration: {CertUtils.posix_to_date(ip_site_posix_exp_date)}\nServer certificate expiration: {CertUtils.posix_to_date(server_cert_posix_exp_date)} \nTelegram certificate expiration: {CertUtils.posix_to_date(telegram_posix_exp_date)}')

    # Replace placeholders with user values
    content = content.replace("{{WIFI_SSID}}", wifi_name)
    content = content.replace("{{WIFI_PASSWORD}}", wifi_password)
    content = content.replace("{{NUMBER_OF_SESSIONS}}", str(len(user_sessions)))
    content = content.replace("{{USER_SESSIONS_ARRAY}}", ',\n  '.join([str(usession.toString()) for usession in user_sessions]))
    content = content.replace("{{STATIC_IP}}", str(static_ip))
    content = content.replace("{{LOCAL_GATEWAY}}", str(gateway))
    content = content.replace("{{SUBNET}}", str(subnet))
    content = content.replace("{{BOT_TOKEN}}", telegram_bot_token)
    content = content.replace("{{CHAT_ID}}", telegram_chat_id)
    content = content.replace("{{SERVER_CERTIFICATE}}", certificate.decode().removesuffix('\n'))
    content = content.replace("{{PRIVATE_KEY}}", private_key.decode().removesuffix('\n'))
    content = content.replace("{{TELEGRAM_CERT}}", telegram_api_cert[0].removesuffix('\n'))
    content = content.replace("{{IP_SITE_CERT}}", ip_site_cert_str.removesuffix('\n'))
    content = content.replace("{{IP_SITE_URL}}", ip_site_url)
    content = content.replace("{{IP_SITE_CERT_EXPIRATION}}", str(ip_site_posix_exp_date))
    content = content.replace("{{SERVER_CERT_EXPIRATION}}", str(server_cert_posix_exp_date))
    content = content.replace("{{TELEGRAM_CERT_EXPIRATION}}", str(telegram_posix_exp_date))

    # print(content)

    # Write the envVariables.h file
    with open(path_sufix + 'envVariables.h', 'w') as file:
        file.write(content)