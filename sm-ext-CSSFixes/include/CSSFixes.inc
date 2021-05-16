#if defined _cssfixes_included
 #endinput
#endif
#define _cssfixes_included

// Map entities to client entity in FireBullets/SwingOrStab ShouldHitEntity.
// Aka. shoot and knife through physboxes that are parented to teammates (white knight, gandalf, horse, etc.)
native void PhysboxToClientMap(char map[2048], bool set);

public Extension __ext_CSSFixes =
{
	name = "CSSFixes",
	file = "CSSFixes.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public __ext_CSSFixes_SetNTVOptional()
{
}
#endif
