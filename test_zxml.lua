local zxml_c = require "zxml.core"
local print_r =require "print_r"
local zxml_parser_excel_xml2003 = zxml_c.zxml_parser_excel_xml2003
local zxml_parser = zxml_c.zxml_parser

local function readfile(path)
    local fd = io.open(path, "r")
    local s = fd:read("a")
    fd:close()
    return s
end

local xml_path = ...
local xml_source = readfile(xml_path)
local t1 = zxml_parser(xml_source)
local t2  = zxml_parser_excel_xml2003(xml_source)
print_r(t1)
print("-----------------")
print_r(t2)