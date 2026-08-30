"""
Corporate-TLS bootstrap.

On managed/enterprise machines a proxy (Zscaler, Netskope, etc.) often
re-signs HTTPS traffic with a company root CA. That CA lives in the Windows
certificate store, so browsers trust it, but Python's bundled `certifi` list
does not, which causes:

    SSL: CERTIFICATE_VERIFY_FAILED - unable to get local issuer certificate

Importing this module makes Python's `ssl` (and therefore requests / urllib3 /
huggingface_hub) validate against the operating system trust store instead of
certifi. It is a no-op if `truststore` is not installed, so it stays safe in
non-corporate environments.
"""

import os


def enable_os_trust_store() -> bool:
    """Route Python TLS verification through the OS trust store. Returns True on success."""
    try:
        import truststore

        truststore.inject_into_ssl()
        # Nudge requests-based libs to use the system store too, in case any
        # code path reads these env vars directly instead of the ssl context.
        os.environ.setdefault("SSL_CERT_FILE", "")
        return True
    except Exception:
        # truststore not installed or injection failed; leave defaults in place.
        return False


enabled = enable_os_trust_store()
