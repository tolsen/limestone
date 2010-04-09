#!/usr/bin/ruby -w
require File.dirname(__FILE__) + '/db_connect'

db_connect do |dbh|
  # define concat() aggregate function
  dbh.do("CREATE AGGREGATE concat( sfunc = textcat, basetype = text, stype = text, initcond = '')");
end
