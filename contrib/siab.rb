#!/usr/bin/ruby
#===============================================================================
#
#         FILE: siab.rb
#
#        USAGE: ruby siab.rb [start|stop|restar|help]
#
#  DESCRIPTION: a ShellInAbox control script and configuration system reader.
#               To auto-configure a shellinabox service create a conf directory
#               in the shellinabox home dir.  The URL will be the name of the file(s)
#               that reside in conf, and the command will be the contents of the file.
#
#               EX configuration file :  conf/nethack
#               conf/nethack contents :  /usr/games/nethack
#
#               Change the value of @siab_home to where you install shellinabox
#      OPTIONS: none
# REQUIREMENTS: ruby
#         BUGS: ---
#        NOTES: ---
#       AUTHOR: Brad Reeves (brad.reeves@openlogic.com)
#      COMPANY: OpenLogic,Inc
#      VERSION: 0.4
#      CREATED: 01/24/2011 14:26:36 MDT
#     REVISION: 1.0
#===============================================================================

@siab_home="/opt/shellinabox"
@urls = Hash.new
@urllist=""
@top_dir=Dir.pwd



def read_configuration()
  Dir.chdir("#{@siab_home}/conf")
  @urllist = Dir["*"]
  @urllist.each do |u|
    @urls[u]=File.open("#{u}","r").readlines.to_s.chomp!
  end
  Dir.chdir("#{@top_dir}")
end

def start
  command_line = ""
  @urls.each_pair do |k,v|
    command_line = command_line + "-s " + "/#{k}:root:root:/:'#{v}' "
  end
  exec("#{@siab_home}/bin/shellinaboxd --background='#{@siab_home}/siab.pid' -c /tmp #{command_line}") if fork.nil?
end

def stop()
  if File.exists?("#{@siab_home}/siab.pid")
    pid=File.open("#{@siab_home}/siab.pid","r").readlines.to_s
    system("kill -9 #{pid}")
    system("rm -rf #{@siab_home}/siab.pid")
  end
end


def restart()
  stop()
  start()
end

def usage()
  puts "Usage: siab.rb [start|stop|restart]"
  exit(0)
end

def get_args()
  case ARGV.size
  when 0
    read_configuration
    restart()
  when 1
    case ARGV[0]
    when "start"
      read_configuration
      restart()
    when "restart"
      read_configuration
      restart()
    when "stop"
      stop()
    when "help"
      usage()
    else
      usage()
    end
  else
      usage()
  end
end

#Main
get_args()
