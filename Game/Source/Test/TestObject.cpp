#include "TestObject.h"

#include <iostream>

void QTestObject::RemoveFriend(int Idx)
{
    if (Idx == 1)
    {
        Friend1 = nullptr;
    }
    else if (Idx == 2)
    {
        Friend2 = nullptr;
    }
    else if (Idx == 3)
    {
        Friend3 = nullptr;   
    }
    else
    {
        std::cout << "Invalid index.\n";       
    }
}
