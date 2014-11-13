require 'mkmf'
require 'open3'



################################################################################
# Configuration
# Set the paths to these programs on your system if they aren't in your PATH
################################################################################
CPPUMAKER = '/usr/lib/libreoffice/sdk/bin/cppumaker'
SETSDKENV = '/usr/lib/libreoffice/sdk/setsdkenv_unix'
# CPPUMAKER = '/Users/ssilver/Desktop/LibreOffice4.3_SDK/bin/cppumaker'
# SETSDKENV = '/Users/ssilver/Desktop/LibreOffice4.3_SDK/setsdkenv_unix'
################################################################################










# IDL Headers
IDL_TYPES = [
  "com.sun.star.beans.MethodConcept",
  "com.sun.star.beans.PropertyAttribute",
  "com.sun.star.beans.XIntrospection",
  "com.sun.star.beans.XIntrospectionAccess",
  "com.sun.star.beans.XMaterialHolder",
  "com.sun.star.container.XEnumerationAccess",
  "com.sun.star.container.XHierarchicalNameAccess",
  "com.sun.star.container.XIndexContainer",
  "com.sun.star.container.XNameContainer",
  "com.sun.star.lang.XMultiServiceFactory",
  "com.sun.star.lang.XServiceInfo",
  "com.sun.star.lang.XSingleComponentFactory",
  "com.sun.star.lang.XSingleServiceFactory",
  "com.sun.star.lang.XTypeProvider",
  "com.sun.star.lang.XUnoTunnel",
  "com.sun.star.reflection.InvocationTargetException",
  "com.sun.star.reflection.ParamMode",
  "com.sun.star.reflection.XConstantTypeDescription",
  "com.sun.star.reflection.XConstantsTypeDescription",
  "com.sun.star.reflection.XEnumTypeDescription",
  "com.sun.star.reflection.XIdlReflection",
  "com.sun.star.reflection.XInterfaceTypeDescription2",
  "com.sun.star.reflection.XTypeDescription",
  "com.sun.star.registry.XRegistryKey",
  "com.sun.star.script.InvocationInfo",
  "com.sun.star.script.MemberType",
  "com.sun.star.script.XInvocation2",
  "com.sun.star.script.XInvocationAdapterFactory2",
  "com.sun.star.script.XTypeConverter",
  "com.sun.star.script.provider.ScriptFrameworkErrorException",
  "com.sun.star.uno.XAggregation",
  "com.sun.star.uno.TypeClass",
  "com.sun.star.uno.XComponentContext",
  "com.sun.star.uno.XWeak",
]

CPPU_INCLUDE = './include'

def missing(item)
  die "couldn't find #{item} (required)"
end

def die(msg)
  puts msg
  exit
end

def find_environment(var)
  checking_for checking_message(var, "environment") do
    !ENV[var].nil?
  end
end


missing(CPPUMAKER) unless find_executable(CPPUMAKER)
missing(SETSDKENV) unless find_executable(SETSDKENV)

# Collected needed environment variables
sdk_home = ENV['OO_SDK_HOME']
ure_home = ENV['OO_SDK_URE_HOME']
base_path = ENV['OFFICE_PROGRAM_PATH']

message "collecting ENV values:\n"
IO.popen("#{SETSDKENV} >/dev/null", "w") {} # Ensure the SDK is set up
res = Open3.popen3(SETSDKENV) do |i, o, e|
  i.puts "echo $OO_SDK_HOME"
  i.puts "echo $OO_SDK_URE_HOME"
  i.puts "echo $OFFICE_PROGRAM_PATH"
  i.close_write
  o.readlines
end
sdk_home, ure_home, base_path = res[-5..-2].map(&:chomp)
message "$OO_SDK_HOME=#{sdk_home}\n"
message "$OO_SDK_URE_HOME=#{ure_home}\n"
message "$OFFICE_PROGRAM_PATH=#{base_path}\n"

message "generating C++ representation for OpenOffice IDL types... "
ure_types = "#{ure_home}/share/misc/types.rdb"
office_types = "#{base_path}/types/offapi.rdb"
IO.popen("#{SETSDKENV} >/dev/null", "w") do |shell|
  shell.puts "#{CPPUMAKER} -C -O#{CPPU_INCLUDE} -T\"#{IDL_TYPES.join(';')}\" \"#{ure_types}\" \"#{office_types}\""
end
message "ok\n"

sdk_lib = "-L\"#{sdk_home}/lib\""
ure_lib = "-L\"#{ure_home}/lib\""
sdk_libs = " -luno_cppuhelpergcc3 -luno_cppu -luno_salhelpergcc3 -luno_sal -lm "

$warnflags = $warnflags.split.reject{|w| w['declaration-after-statement'] || w['implicit-function-declaration'] }.join(" ") # Remove invalid warnings
$INCFLAGS << " -I#{sdk_home}/include -I#{CPPU_INCLUDE} "
$LOCAL_LIBS << "#{sdk_lib} #{ure_lib} #{sdk_libs}"
$CFLAGS << " -fno-strict-aliasing -DUNX -DGCC -DLINUX -DCPPU_ENV=gcc3 "
$CPPFLAGS = " -std=c++11 #{$CFLAGS}"

create_makefile('rubyuno/rubyuno')
