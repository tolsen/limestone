#!/usr/bin/ruby -w

require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  dbh.do("UPDATE resources SET type_id = type_id - 1 WHERE type_id > 7")
end
