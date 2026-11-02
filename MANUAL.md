The Kindle XCOM Game Manual

This manual is written to be heard, not read. It is all plain sentences, and every rule in it is exactly what the game does.

What the game is

You have four soldiers and there are four maps to fight over, one after the other. Each map holds five aliens, most of them hidden in the fog. You take turns. On your turn each of your soldiers may walk and then shoot, or walk instead of shooting, or dig in and wait. When you are finished, the aliens move and shoot back. When every alien on a map is down you move on to the next map. Clear the fourth map and you have won the campaign. Lose every soldier on any map and the whole run is over.

Getting it onto the Kindle

Sit at the computer with the Kindle on the same Wi-Fi. Then run the script called run on kindle dot sh. If the Kindle has a different address from the usual one, put the address after it, and press return.

The script copies the game and its voice clips across, it takes the touch screen away from the Kindle's screen mirror, and it starts the game. When you quit, the script puts the Kindle back to normal and gives you your books back. Nothing is downloaded and the game makes no network calls, so you only need the two machines on the same network.

What you see

The top of the screen shows the turn number and the word your turn. Next to that is which mission you are on, from M1 of 4 to M4 of 4, then a count of how many aliens you can currently see, and the random seed for this game, which is only useful when something has gone wrong.

Then comes the map, twelve squares wide and thirteen squares tall. Pale squares are grass, and long smooth squares are road. Darker squares are trees, and those slow you down. Black shapes with hard edges are city walls, and you cannot walk through those. You will also see rocks, crates, and water, and none of those can be crossed either. Low bushes are walkable and cost one point like grass, but they give you no cover. Tall mountains block walking and sight both ways, and give no cover either. Any square you have never seen is covered by a flat dark grey wash, which is fog.

Every soldier and every alien stands in a square with a small bar underneath showing how much health is left. Your people carry a badge with a black ring, and the aliens carry a badge with a white ring inside a black square.

Under the map are three lines of the game log, which is the last few things that happened, and it is also the exact text the game speaks out loud. Below that is the detail line for whichever soldier you have selected, showing health, actions left, aim, damage, armour, movement, and weapon range.

Along the bottom are four buttons: end, watch, hunker, and heal. In the top right corner is quit, which turns into sure when you tap it. Just left of quit is the sound button: the game opens with the sound off and the button reads unmute — tap it to bring the speaker back (the log prints on screen either way). Tap it again to become mute and silence the speaker, even mid-sentence.

How a turn works

Tap one of your soldiers to select them. The soldier's square gets a double frame around it, and if they can shoot, every alien they could hit gets its own frame too. Small thin rectangles on the ground show where that soldier can walk. A thin frame is an ordinary walk. A thick frame is a long run.

Tap a framed square and the soldier walks there. Walking is free, but each square costs movement points. Grass and road cost one. Trees and rubble cost two. All of your soldiers have four movement points, so a run of six squares is possible along open ground, and that is a long run.

A long run spends both of that soldier's actions, so they cannot shoot afterwards. Ordinary walking leaves the shot available. Once a soldier has used their walk, the small boxes disappear, and you can still shoot.

You do not have to use every soldier. Anything you leave undone is simply skipped. When you are done, tap end, and the aliens take their turn.

Shooting

Tap an alien your soldier has selected, and it lights up with a frame. Two big buttons appear: back, and fire, with a percentage on it. That percentage is your chance to hit. Tap fire to take the shot, or tap back to think again. You can also tap the alien a second time to shoot.

The chance is worked out in four steps. Start with your soldier's aim, which is between sixty-five and eighty-five depending on who it is. If the soldier has not moved this turn, add twenty, but only some soldiers get that bonus, and the sniper does. Then subtract six for every square of distance beyond two squares. Then subtract the alien's cover. A target standing in trees has forty cover. A target standing in rubble has twenty. If a wall, a rock, a tree, or a crate sits between you and the target, right next to the target, that is forty cover as well. Finally, an alien that has hunkered down adds another forty. The result is never worse than five percent and never better than ninety-five.

If the dice roll is under your chance, the shot hits. One in ten hits is a critical, which does two extra damage. Then the target's armour is taken off the damage, and what remains comes off their health. If the armour eats all of it, the log says the target soaked it.

Trees are the best bargain on the map. They cost two movement points to enter, they give forty cover, and they block sight, which means an alien on the other side of them cannot see you and cannot shoot you.

The four buttons

Watch means overwatch. That soldier gives up the rest of their turn, and during the alien turn they fire at the first alien that moves into view. One alien, one shot. Any alien outside their range or line of sight is not shot at, so watch is best spent watching a direction you expect trouble from.

Hunker makes a soldier much harder to hit, adding forty to the attacker's difficulty, and it ends that soldier's turn.

Heal belongs to the medic only. It works on a friend standing next to the medic, it gives back three health, and there are exactly two of them per mission. It spends the medic's shot for that turn.

End finishes your turn and lets the aliens move.

Quit, then sure, leaves the game and gives the Kindle its book screen back.

Your squad

You start along the bottom edge of the map, from left to right as soldier, sniper, heavy, and medic.

The soldier is the generalist. Six health, aim of seventy, damage of two to four, and a range of eight squares.

The sniper is the marksman. Five health, aim of eighty-five, damage of four to six, and a range of eleven squares, the longest on the map. The sniper is the one who gets the extra twenty for shooting without moving, so leave them still and let them work from the back.

The heavy is the tough one. Eight health, armour of two, aim of sixty-five, damage of three to five, and a range of seven. Hits from the heavy pin the target, and pin any of the target's friends standing right next to it. A pinned unit cannot shoot on its next turn, whichever side it is on.

The medic is the support. Six health, aim of seventy, damage of two to three, a range of seven, and a pair of heals that give back three health each.

The campaign

The four maps are played in order, and the header tells you which one you are on. Each map opens with a spoken line before the fighting starts.

Mission one is FIRST CONTACT, wipe them out. It is the old island map: ruins in the corners, a river across the middle, and a road crossing it.

Mission two is THE FORD, cross at the shallows. Forest on both banks, and a wide river you can only cross where two shallows run down it, because everywhere else the water is too deep.

Mission three is THE LONG ROAD, stay in cover. A straight road runs across the middle of the map from edge to edge, so both sides have to come to it. Trees, rocks and ruins stand off to the sides.

Mission four is THE CROWN, take the high ground. A ring of mountains fills the upper middle of the map with a forest floor around it. The mountains block walking and sight, so you go around them, not over them.

You start every mission along the bottom edge, in the same order: soldier, sniper, heavy, medic. Each mission has its own five aliens, and the alien sniper on every map is placed far enough away that it cannot reach you on the first turn.

The aliens

You are facing five of them on each map.

The sectoid is the small one. Five health, aim of sixty-five, damage of two to four. When a sectoid hits you, there is a one in five chance your soldier panics and freezes completely for their next turn.

The alien sniper is the dangerous one. Five health, aim of eighty-five, damage of four to six, and a range of eleven. Do not stand in the open in front of it.

The mutant is a brute. Ten health, armour of two, damage of three to five, but it can only attack from an adjacent square, so it has to come to you. Shoot it before it arrives.

The assault alien is quick and ordinary. Seven health, armour of one, aim of seventy-five, damage of three to five.

The drone flies. It ignores the ground completely, so trees, walls, and water do not slow it or stop it, and it has no cover at all. Four health only, but it sees nine squares and shoots from six.

Sound

The game speaks. Every line that scrolls in the log is also read out loud, word by word, so you can play without looking at the text. A shot makes a bang, a hit makes a thud, a miss makes a softer noise, a unit going down has its own sound, and the turn change and the win and the loss have their own too.

If the sound folder is missing on the Kindle, the game says so and runs silently rather than pretending. If you want quiet, start it with mute.

How to win

Wipe out all five aliens on a map and you move to the next one. Clear the fourth map and you have won the campaign. Lose all four soldiers on any map and the run is over. There is no timer and no score, so patience is free.

A few things that make it much easier

Move one soldier at a time and look at what the move uncovers. Walking into the fog reveals squares, and it is easy to walk straight into three aliens you had not seen.

End your turn with the squad hunkered or on watch when you expect contact, and end it with them free when you are still exploring and need the movement.

Use trees. Shooting from trees and making the aliens come through open ground is most of the skill in this game.

Shoot the mutant and the alien sniper first and the sectoids last.

You can always quit and start again. Every game prints its random seed at the top of the screen, so a fight you want to study can be replayed exactly by starting with that same seed.
