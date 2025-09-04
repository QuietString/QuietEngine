#pragma once

// These macros are compile-time no-ops. The generator reads them from raw source files.
#ifndef QMETA_MACROS_H
#define QMETA_MACROS_H

// Put this right before a class/struct you want to be scanned (optional; auto-opt-in if the class contains QPROPERTY/QFUNCTION)
#define QREFLECT() /* marker */

// Put this right before a field declaration. Example:
//   QPROPERTY()
//   int Health = 100;
#define QPROPERTY(...) /* marker */

// Put this right before a member function declaration. Example:
//   QFUNCTION()
//   int AddHealth(int Delta) const;
#define QFUNCTION(...) /* marker */

#endif