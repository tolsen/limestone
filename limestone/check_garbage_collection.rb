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

# returns id of the resource identified by url
def get_resource_id(dbh, url)
  res_id = 2 # root collection id
  url.split('/').each do |tok|
    row = dbh.select_one("SELECT resource_id FROM binds WHERE collection_id = #{res_id.to_s} " +
                         "AND name = '#{tok}' ")
    if row then
      res_id = row[0]
    else
      res_id = 0
      break
    end
  end
  res_id
end

# returns the number of unique children at(and including) resource identified by url
def get_num_unique_children(dbh, url)
  res_id = get_resource_id(dbh, url)
  return 0 if res_id == 0
  parents = Set.new
  parents.add(res_id)
  parents_str = res_id.to_s

  begin
    sth = dbh.prepare("SELECT resource_id FROM binds WHERE collection_id IN (#{parents_str})")
    sth.execute
    children = Set.new
    while row = sth.fetch do
      children.add(row[0])
    end
    sth.finish
    new_children = children - parents
    parents.merge(new_children)
    parents_str = new_children.inject(parents_str) {|p_str, new_p| "#{p_str}, #{new_p.to_s}"}
  end while new_children.size > 0
  parents.size
end

begin
  # connect to the SQL server
  dbh = DBI.connect("DBI:#{@dbi_dbd_driver}:database=#{@db_name};host=#{@db_hostname};port=#{@db_port};", @db_username, @db_password)
  # get the number of resources from the table
  row = dbh.select_one("SELECT COUNT(*) FROM resources")
  @rows = row[0]
  @num_deltav_resources = get_num_unique_children(dbh, "history")
rescue DBI::DatabaseError => e
  puts "An error occurred"
  puts "Error code: #{e.err}"
  puts "Error message: #{e.errstr}"
ensure
  # disconnect from server
  dbh.disconnect if dbh
end

@num_resources_left_behind = @rows - @num_deltav_resources
expected_num_resources = 25
if @num_resources_left_behind != expected_num_resources then
  puts "Unexpected number(" + @num_resources_left_behind.to_s + ") of rows left behind"
  puts "Other than the resources rooted at /history, we expect #{expected_num_resources} resources to be left behind."
  exit(-1)
end
