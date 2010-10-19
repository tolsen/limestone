ARGF.each_line do |l|
    print l.gsub(/.*\[(0x.*)\]/) { |address| %x[addr2line -e aproot/bin/httpd -f #{$1}].gsub(/\n/, '') }
end
