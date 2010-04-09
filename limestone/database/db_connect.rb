# you need to install libdbi-ruby, libdbd-mysql-ruby and libdbd-pg-ruby debian packages
require 'dbi'

def get_namespace_id(dbh, namespace)
  row = dbh.select_one("SELECT id FROM namespaces WHERE name = '#{namespace}'")
  ns_id = row ? row[0] : -1
end

def insert_namespace(dbh, namespace)
  nrows = dbh.do("INSERT INTO namespaces(name) VALUES ('#{namespace}')")
  raise "Error inserting namespace #{namespace}" unless nrows == 1
end

def insert_acl_privilege(dbh, ns_id, name, abstract, par_id, lft, rgt)
  nrows = dbh.do("INSERT INTO acl_privileges(priv_namespace_id, name, abstract, parent_id, lft, rgt) VALUES (#{ns_id}, '#{name}', 'f', #{par_id}, #{lft}, #{rgt})")
  raise "Error inserting acl_privilege #{name}" unless nrows == 1
end

# yields dbi handle
def db_connect &block
  db_hostname = db_username = db_password = db_name = nil

  dbi_dbd_driver = "pg"
  db_hostname = ENV['LIMESTONE_PGSQL_DB_HOST']
  db_port = ENV['LIMESTONE_PGSQL_DB_PORT']
  db_port = 5432 if db_port.nil?
  db_username = ENV['LIMESTONE_PGSQL_DB_USER']
  db_password = ENV['LIMESTONE_PGSQL_DB_PASS']
  db_name = ENV['LIMESTONE_PGSQL_DB_NAME']
  
  # connect to the SQL server
  dbh = DBI.connect("DBI:#{dbi_dbd_driver}:database=#{db_name};host=#{db_hostname};port=#{db_port}", db_username, db_password)

  yield dbh
rescue DBI::DatabaseError => e
  puts "An error occurred"
  puts "Error code: #{e.err}"
  puts "Error message: #{e.errstr}"
ensure
  # disconnect from server
  dbh.disconnect if dbh
end


    

  
