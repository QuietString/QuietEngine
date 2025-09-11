#include "TestObject.h"

#include <iostream>

void QTestObject::RemoveFriend(int Idx)
{
    switch (Idx)
    {
        
        case 1: Friend1 = nullptr; break;
        case 2: Friend2 = nullptr; break;
        case 3: Friend3 = nullptr; break;
        case 4: Friend4 = nullptr; break;
        case 5: Friend5 = nullptr; break;
        default:
        std::cout << "Invalid index.\n";
        break;
    }
}
