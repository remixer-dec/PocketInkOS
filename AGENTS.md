- We prioritize simplicity over complexity
- We try to make things modular in order to improve future maintanibility
- We write comments where its needed and not obvious
- We prefer minimalism and structural coherence
- Never install the Arduino IDE inside the container; use `tools/container-compile.sh` for compilation. No need to separately run ENABLE_NETWORK_APPS=1.
- Never read `.env` or `src/secrets_config.h`; use `.env.example` as the source for environment variable conventions. If you run a compile script that reads .env and src/secrets_config.h, thats totally fine.

