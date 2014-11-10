1. sudo apt-get install libreoffice-dev
2. Update ext/rubyuno/extconf.rb with appropriate paths:
  - On Ubuntu 14.10: `/usr/lib/libreoffice/sdk/bin/cppumaker` and `/usr/lib/libreoffice/sdk/setsdkenv_unix`
2. ruby ext/rubyuno/extconf.rb
3. make
3. ???
4. Profit
