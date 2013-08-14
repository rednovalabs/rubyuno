# coding: utf-8
#
#  Copyright 2011 Tsutomu Uchino
# 
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#  
#    http://www.apache.org/licenses/LICENSE-2.0
#  
#  Unless required by applicable law or agreed to in writing,
#  software distributed under the License is distributed on an
#  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#  KIND, either express or implied.  See the License for the
#  specific language governing permissions and limitations
#  under the License.

require 'rubyuno/rubyuno'

module Uno
  Rubyuno.uno_require 'com.sun.star.connection.NoConnectException'
  Rubyuno.uno_require 'com.sun.star.connection.ConnectionSetupException'
  
#
# Helps to connect to the office by RPC with UNO protocol.
# 
# These environmental variables should be set before to load 'runo' 
# module.
# URE_BOOTSTRAP specifies to fundamental(rc|.ini) placed under 
#   openoffice.org3/program directory.
# LD_LIBRARY_PATH path to URE library and program directory of the office. 
# 
  module Connector
  
    class NoConnectionError < StandardError
    end
    
    PIPE_NAME_PREFIX = "rubypipe_"
    
    @@sleep_time = 2.0
    @@retry = 5
    
    attr_accessor :sleep_time, :retry
    
    # Read Professional UNO chapter of Developer's Guide about 
    # UNO Remote protocol.
    def self.bootstrap(options = {})
      options = {
        :office => 'soffice',
        :type => 'socket',
        :host => '127.0.0.1',
        :port => 2083,
        :nodelay => false
      }.merge(options)

      url, argument = self.url_construct(options[:type], options[:host], options[:port], options[:pipe_name], options[:nodelay])

      r = self.resolver_get
      c = nil
      n = 0

      begin
        c = self.connect(url, r)
      rescue Rubyuno::Com::Sun::Star::Uno::Exception => e
        raise e if e.instance_of?(Rubyuno::Com::Sun::Star::Connection::ConnectionSetupException)

        n += 1
        (raise NoConnectionError,"") if n > @@retry

        if options[:spawn_cmd]
          system options[:spawn_cmd]
        else
          spawn(ENV, options[:office], argument)
        end

        sleep(@@sleep_time)

        retry
      end

      return c
    end
    
    def self.connect(url, resolver = nil)
      resolver = self.resolver_get unless resolver
      return resolver.resolve(url)
    end

    def self.url_construct(type = "socket", host = "127.0.0.1", port = 2083, pipe_name = nil, nodelay = false)
      case type
        when "socket"
          part = "socket,host=#{host},port=#{port}," +
                    "tcpNoDelay=#{nodelay ? 1 : 0};urp;"
          url = "uno:#{part}StarOffice.ComponentContext"
          argument = "-headless -nologo -nofirststartwizard -accept=#{part}StarOffice.ServiceManager"
        when "pipe"
          pipe_name = "#{PIPE_NAME_PREFIX}#{srand * 10000}" unless pipe_name
          part = "pipe,name=#{pipe_name};urp;"
          url = "uno:#{part}StarOffice.ServiceManager"
          argument = "-headless -nologo -nofirststartwizard -accept=#{part}StarOffice.ComponentContext"
        else
          raise ArgumentError, "Illegal connection type (#{type})"
      end

      return url, argument
    end

    def self.resolver_get
      ctx = Rubyuno.get_component_context
      return ctx.getServiceManager.createInstanceWithContext("com.sun.star.bridge.UnoUrlResolver", ctx)
    end
  end
end