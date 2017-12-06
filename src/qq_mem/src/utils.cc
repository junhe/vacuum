#include "utils.h"

#include <iostream>
#include <sstream>
#include <iterator>
#include <cassert>
#include <stdexcept>

namespace utils {

// To split a string to vector
// Not sure how good it is for unicode...
const std::vector<std::string> explode(const std::string& s, const char& c)
{
    std::string buff{""};
    std::vector<std::string> v;
    
    for(auto n:s)
    {
        if(n != c) buff+=n; else
        if(n == c && buff != "") { v.push_back(buff); buff = ""; }
    }
    if(buff != "") v.push_back(buff);
    
    return v;
}

// TODO: Replace this boost tokenizer
const std::vector<std::string> explode_by_non_letter(const std::string &text) {
    std::istringstream iss(text);
    std::vector<std::string> results((std::istream_iterator<std::string>(iss)),
                                     std::istream_iterator<std::string>());
    return results;
} 

const std::vector<std::string> explode_strict(const std::string &line, const char& sep) {
    std::string buff{""};
    std::vector<std::string> vec;
    
    for(auto ch:line)
    {
        if(ch != sep) {
            buff += ch; 
        } else { 
            vec.push_back(buff); 
            buff = "";
        }
    }
    vec.push_back(buff);
    
    return vec;
}

// Parse offsets from string
void handle_positions(const std::string& s, Offset& this_position) {
    std::size_t pos_split = s.find(",");
    this_position = std::make_tuple(std::stoi(s.substr(0, pos_split)), std::stoi(s.substr(pos_split+1)));
    // construct position
    return;
}

void handle_term_offsets(const std::string& s, Offsets& this_term) {
    std::string buff{""};

    for(auto n:s) {
        // split by ;
        if(n != ';') buff+=n; else
        if(n == ';' && buff != "") {
            Offset this_position;
            // split by ,
            handle_positions(buff, this_position);
            this_term.push_back(this_position);
            buff = "";
        }
    }

    return;
}

const std::vector<Offsets> parse_offsets(const std::string& s) {
    std::vector<Offsets> res;
    
    std::string buff{""};

    for(auto n:s)
    {
    // split by .
        if(n != '.') buff+=n; else
        if(n == '.' && buff != "") {
            Offsets this_term;
            handle_term_offsets(buff, this_term);
            res.push_back(this_term);
            buff = "";
        }
    }
    return res;
}

///////////////////////////////////////////////
// LineDoc
///////////////////////////////////////////////

LineDoc::LineDoc(std::string path) {
    infile_.open(path);

    if (infile_.good() == false) {
        throw std::runtime_error("File may not exist");
    }

    std::string line; 
    std::getline(infile_, line);
    std::vector<std::string> items = explode(line, '#');
    // TODO: bad! this may halt the program when nothing is in line
    items = explode_by_non_letter(items.back()); 
    col_names_ = items;
}

bool LineDoc::GetRow(std::vector<std::string> &items) {
    std::string line;
    auto &ret = std::getline(infile_, line);

    if (ret) {
        items = explode_strict(line, '\t');
        return true;
    } else {
        return false;
    }
}





} // namespace util
