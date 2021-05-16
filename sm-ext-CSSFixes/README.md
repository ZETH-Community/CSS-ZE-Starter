# Counter-Strike: Source bug fixes
### Fixes crash in filter_activator_*->TestActivator() when activator isn't valid anymore.
### Fixes game_ui lag and player_speedmod turning off your flashlight.

# Patchnotes
### Thanks to the leaked Source Engine 2007 sourcecode.

#### File: se2007/game/server/game_ui.cpp
##### Line 292:

```C
void CGameUI::Think( void )
[...]
	pPlayer->AddFlag( FL_ONTRAIN );
[...]
```
Replaced **pPlayer->AddFlag( FL_ONTRAIN );** with **NOP** to fix prediction issues
while having game_ui active.

#### File: se2007/game/server/player.cpp
##### Line: 7587

```C
void CMovementSpeedMod::InputSpeedMod(inputdata_t &data)
[...]
			// Turn off the flashlight
			if ( pPlayer->FlashlightIsOn() )
			{
				pPlayer->FlashlightTurnOff();
			}
[...]
```

**NOP**'d out the block.