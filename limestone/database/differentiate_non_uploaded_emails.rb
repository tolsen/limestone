#!/usr/bin/ruby -w
require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  rows = dbh.select_all("SELECT principal_id, email FROM users" +
                        " WHERE lower(email) IN" +
                        " (SELECT lower(email) FROM USERS" +
                        "  GROUP BY lower(email) HAVING count(*) >= 2)" +
                        " AND principal_id NOT IN" +
                        " (SELECT principal_id FROM lime_profiles)" +
                        " AND pre_unique_email IS NULL")

  rows.each do |row|
    email = row['email']
    email_parts = email.split '@'
    raise "email should have exactly one @: #{email}" unless email_parts.size == 2
    local_part, domain = email_parts
    new_email = "#{local_part}+1@#{domain}"
    dbh.do("UPDATE users SET email = '#{new_email}' " +
           "WHERE principal_id = #{row['principal_id']}")
  end
end
