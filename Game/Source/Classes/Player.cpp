#include "Player.h"

int QPlayer::AddHealth(int Delta) { Health += Delta; return Health; }
void QPlayer::SetWalkSpeed(float Speed) { WalkSpeed = Speed; }