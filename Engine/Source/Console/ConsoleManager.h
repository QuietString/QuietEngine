#pragma once
#include <string>
#include <vector>


namespace ConsoleManager
{
    // Simple tokenizer that honors quoted tokens.
    std::vector<std::string> Tokenize(const std::string& s);
    
    // Execute one command line. Returns true if the command was recognized.
    bool ExecuteCommand(const std::string& Line);
}

