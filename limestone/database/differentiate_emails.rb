#!/usr/bin/ruby -w
require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  rows = dbh.select_all("SELECT principal_id, email FROM users ORDER BY principal_id");

  emails = {}
  
  rows.each do |row|
    email = row['email']
    if emails.include? email
      old_count = emails[email]
      email_parts = email.split '@'
      raise "email should have exactly one @: #{email}" unless email_parts.size == 2
      local_part, domain = email_parts
      new_email = "#{local_part}+#{old_count}@#{domain}" 
      dbh.do("UPDATE users SET email = '#{new_email}' " +
             "WHERE principal_id = #{row['principal_id']}")
      emails[email] += 1
    else
      emails[email] = 1
    end
      
  end
  
end
