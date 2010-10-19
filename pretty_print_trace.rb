ARGF.each_line do |l|
    print l.gsub(/(.*)\[(0x.*)\]/) { |address| 
        original_line = $1 + "[" + $2 + "]"
        source_line = %x[addr2line -e aproot/bin/httpd -f #{$2}].gsub(/\n/, '');

        if source_line.match(/\?/)
            original_line
        else
            source_line
        end
    }
end
