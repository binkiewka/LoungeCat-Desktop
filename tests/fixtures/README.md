# Loopback TLS fixtures

The localhost and paste-local certificates and RSA keys in this directory are public, disposable test material, not application credentials. They are used only by the loopback IRC server in the regression suite.

The client explicitly trusts this certificate in its temporary test configuration and still performs peer verification. Production trust stores and TLS checks are not changed.
