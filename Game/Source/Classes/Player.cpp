#include "Player.h"

int QPlayer::AddHealth(int Amount) { Health += Amount; return Health; }
void QPlayer::SetWalkSpeed(float Speed) { WalkSpeed = Speed; }