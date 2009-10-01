#!/usr/bin/ruby -w
# you need to install libdbi-ruby, libdbd-mysql-ruby and libdbd-pg-ruby debian packages
require 'optparse'
require 'set'

begin
  require 'dbi'
rescue LoadError => e
  puts "Please install ruby-dbi. See http://ruby-dbi.rubyforge.org/"
  exit(0)
end

@db_hostname = @db_port = @db_username = @db_password = @db_name = nil

OptionParser.new do |opts|
  opts.banner = "Usage: check_garbage_collection.rb [options]"
  
  opts.on("-d", "--dbd-name DBI_DBD_NAME", [:mysql, :pg], "RubyDBI SQL Driver name") do |d|
    @dbi_dbd_driver = d
  end
  opts.on("-n", "--hostname [SERVER_ADDRESS]", "SQL Server Address") do |n|
    @db_hostname = n
  end
  opts.on("-P", "--port [SERVER_PORT]", "SQL Server port") do |n|
    @db_port = n
  end
  opts.on("-u", "--user [USERNAME]", "SQL Server Username") do |u|
    @db_username = u
  end
  opts.on("-p", "--password [PASSWORD]", "SQL Server Password") do |p|
    @db_password = p
  end
  opts.on("-l", "--limestone-db [LIMESTONE_DATABASE_NAME]", "Limestone Database Name") do |n|
    @db_name = n
  end
  opts.on_tail("-h", "--help", "Show this message") do
    puts opts
    exit
  end
end.parse!

@db_hostname = ENV['LIMESTONE_HOST'] if @db_hostname.nil?
@db_username = ENV['LIMESTONE_USER'] if @db_username.nil?
@db_password = ENV['LIMESTONE_PASS'] if @db_password.nil?
@db_name = ENV['LIMESTONE_DB'] if @db_name.nil?

@db_port = 5432 if @db_port.nil?
@plog_schema = 'plog_' + @db_username

def get_num_log_entries(dbh)
  row = dbh.select_one("SELECT COUNT(*) FROM #{@plog_schema}.logdata");
  row[0]
end

begin
  # connect to the SQL server
  dbh = DBI.connect("DBI:#{@dbi_dbd_driver}:database=#{@db_name};host=#{@db_hostname};port=#{@db_port};", @db_username, @db_password)

  @initial_entries = get_num_log_entries(dbh)

  pwd = File.dirname(__FILE__)
  `cat #{pwd}/sample_plog | LIMESTONE_PLOG_FILTERED_IPS='127.0.0.1 127.0.1.1' #{pwd}/../plog.pl -user #{@db_username} -password #{@db_password} -database #{@db_name} -host #{@db_hostname} -port #{@db_port} -schema #{@plog_schema}`

  @final_entries = get_num_log_entries(dbh)

rescue DBI::DatabaseError => e
  puts "An error occurred"
  puts "Error code: #{e.err}"
  puts "Error message: #{e.errstr}"
ensure
  # disconnect from server
  dbh.disconnect if dbh
end

if @final_entries != @initial_entries + 1 then
  puts "Found #{@final_entries} log entries, #{@initial_entries + 1} expected."
  exit(-1)
end
