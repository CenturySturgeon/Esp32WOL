import ssl
import socket
from typing import Dict
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives import serialization
import datetime

def __get_certificate_data(hostname: str, port: int = 443) -> Dict | None:
    """Gets the certificate details of the provided website and returns it as a dictionary."""
    context = ssl.create_default_context()
    with socket.create_connection((hostname, port)) as sock:
        with context.wrap_socket(sock, server_hostname=hostname) as sslsock:
            certificate = sslsock.getpeercert()
            return certificate

def get_certificate(hostname: str, port: int = 443) -> tuple[str, Dict | None]:
    """Gets the certificate and its data of the specified website."""
    cert = ssl.get_server_certificate((hostname, 443))
    certData = __get_certificate_data(hostname, port)
    return (cert, certData)


############################################# X509 Certificate & Private Key #############################################
# For more info on how to create an X509 self signed certificate visit https://cryptography.io/en/latest/x509/

def create_private_key(size: int = 2048):
    """Creates and returns a private key of the specified size. -> RSAPrivateKey"""
    private_key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=size,
        backend=default_backend()
    )
    return private_key

def create_certificate_and_private_key(private_key_size: int, country_name: str, state_or_province_name: str, locality_name: str, organization_name: str, common_name: str, cert_validity_days: int = 365) -> tuple[bytes, bytes]:
    """
    Returns an X509 certificate and private key.
    
    Params:
        private_key_size (int): The size for the key in bits.
        country_name (str): The name of the country for the certificate.
        state_or_province_name (str): The name of the state or province for the certificate.
        locality_name (str): The name of the city for the certificate.
        organization_name (str): The name of the organization for the certificate.
        cert_validity_days (int): How many days for the certificate to expire since its creation.

    Returns
        tuple(bytes, bytes): Returns the certificate and key in bytes objects. 
    """
    # Generate a private key
    private_key = create_private_key(private_key_size)

    # Create a subject for the certificate using the variables
    subject = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, country_name),
        x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, state_or_province_name),
        x509.NameAttribute(NameOID.LOCALITY_NAME, locality_name),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, organization_name),
        x509.NameAttribute(NameOID.COMMON_NAME, common_name),
    ])


    # Set certificate validity
    validity_period = datetime.timedelta(days=cert_validity_days)
    now = datetime.datetime.utcnow()
    not_valid_before = now
    not_valid_after = now + validity_period

    # Create a self-signed certificate
    builder = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .public_key(private_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(not_valid_before)
        .not_valid_after(not_valid_after)
    )

    # Sign the certificate with the private key
    certificate = builder.sign(
        private_key=private_key,
        algorithm=hashes.SHA256(),
        backend=default_backend()
    )

    # Serialize the certificate to PEM format
    certificate_pem = certificate.public_bytes(
        encoding=serialization.Encoding.PEM
    )

    # Serialize the private key to PEM format
    private_key_pem = private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.TraditionalOpenSSL,
        encryption_algorithm=serialization.NoEncryption()
    )

    # Print or save the certificate and private key to files
    # print(certificate_pem.decode())
    # print(private_key_pem.decode())

    return (certificate_pem, private_key_pem)

def date_to_posix(date_str):
    """Given a date string in the format 'MM DD hh:mm:ss YYYY TZ' return its POSIX time (the number of seconds since January 1, 1970, 00:00:00 (UTC))."""
    # Define the format of the input date string
    date_format = "%b %d %H:%M:%S %Y %Z"
    
    # Parse the input date string into a datetime object
    dt = datetime.datetime.strptime(date_str, date_format)
    
    # Convert the datetime object to POSIX time (Unix timestamp)
    posix_time = dt.timestamp()
    
    return int(posix_time)

def posix_to_date(posix_timestamp):
    return datetime.datetime.utcfromtimestamp(posix_timestamp)

def cert_posix_expiration_date(certificate: bytes) -> int:
    """Returns the certificate's expiration date in POSIX time (the number of seconds since January 1, 1970, 00:00:00 (UTC))."""
    # Assuming cert_bytes is your X.509 certificate stored in bytes variable
    cert = x509.load_pem_x509_certificate(certificate, default_backend())

    # Format for date_to_posix() function
    output_date_format = "%b %d %H:%M:%S %Y %Z"

    # Extracting the expiration date
    expiration_date = date_to_posix(cert.not_valid_after_utc.strftime(output_date_format))

    return expiration_date

def save_cert_and_key(cert: bytes, key: bytes, cert_name: str, key_name: str, file_extension=".pem"):
    # Save to files
    with open(cert_name + file_extension, "wb") as f:
        f.write(cert)
    with open(key_name  + file_extension, "wb") as f:
        f.write(key)
