#include "Player.h"

int Player::AddHealth(int Delta) { Health += Delta; return Health; }
void Player::SetWalkSpeed(float Speed) { WalkSpeed = Speed; }