# Bit Library

This is a place to list bits that the community has found useful, for each bit add materials and uses that it was effective for as well as speeds and recommending settings. 

Your Maslow kit needs a router DewaltDWP611 to work as well as a cutting bit to go in the router.  The Dewalt 611 has a very exact 1/4 inch hole in the chuck.  As sold it can only use router bits with a 1/4 in shaft. 

**To change a Router bit**, you will need to press in towards the center of the router on a yellow button that is in a semicircular recess in the bottom Maslow4 Clamp ring. This button pushes a metal rod into a cut out space on the shaft of the router to hold the shaft in place while you use a wrench to loosen the chuck nut which will let the router bit slide in and out.  This is a lot for just two hands to do. Maslow community members have designed a 3D printable button pusher stub to push into the hole to hold the button down while changing the bit. Be sure to remove it before starting. <https://www.maslowcnc.com/the-not-shop>


**The most common cutting bit is a 1/4 in 2 flute spiral upcutting bit**.  These are available in the Maslow website shop and in most hardware stores.  
<https://shop.maslowcnc.com/collections/router-bits-and-accessories>  
This bit will cut a 1/4 inch groove in plywood or other material, part way through to make pocket cuts or all of the way through to cut around the profile of a piece. It can also drill a 1/4 in hole by pecking in and out of the hole in stages but not very well. Buy one of these to get started. 


The 1/4 inch bit limits you by resolution and shape, many people buy a more delicate 1/8 upcut spiral to do smaller details.  From there you could do all sorts of things. V cutters are good for engraving signs, Longer bits are good for cutting through soft styrofoam, stubby drill bits (regular length drill bits don't quite fit)  for drilling holes.  Rounded bits for sculpting surfaces.  There are many options. 


**Two common router bit adapters that Maslow community members have found useful are a 1/4 to 1/8 inch chuck adaptor** so that you can use 1/8 router and carving bits (common size for Dremel tools and widely available)  And then an alternate chuck tightening system and nut as it is a bit challenging to change router bits in the tight space inside the Maslow. Some of these like MuscleChuck use a hex wrench or other tool, others use different tightening systems that also may be easier to use. 

Router bits, feeds and speeds will vary greatly with material, bit size, humidity and type of cut.  As a place to get started forum member TDA put together this list of important ideas: 
<https://forums.maslowcnc.com/t/routers-and-bits-that-can-be-usted-on-the-maslow-4/19984/5>

**Chipload:**
This is the widest part of the chip being made per flute per rotation of the bit. This determines a great deal in the cut and the effects of the cut. A short incomplete list would be force, heat making it to the other components, cut quality, and tool life. To calculate tool chipload from a feed and speed: Chipload = feed / RPM / number of flutes.

**Runout:**
This is how much the tool spins off the center axis of the cutter (bit wobble). It’s an important factor especially in multi-flute tools as this number will get added and subtracted to chipload to various flutes of the bit.

**Minimum chipload:**
For every material we have a minimum chipload to actually cut. This is determined by both the material’s ability to support itself and a part of the bit that will almost never be listed called the edge radius. For most soft materials (less hard than metal) we don’t really have to worry about the edge radius except maybe for some tools that are designed for metal. If we cut less than this minimum then what will happen is instead of cutting the chip, it will push a partially formed chip out of the way or “grind” / “rub” away the material. This causes an increase in both cutting force and heat vs cutting. After the minimum we will also have chiploads that produce better cuts based on the tool and material.

Another thing to consider in chipload is that the primary source of heat (if not rubbing) is in the deformation of what becomes the chip. So the smaller the chipload the higher the heat that makes it to the material and tool. So even if we are cutting there are advantages to a larger chipload for tool life and cut quality.

Our maximum will almost always be determined by the failure point of the material, flute of the tool, or machine. These are all limited by cutting forces. In other words, when is the cutting force and direction of that force causing a failure in one of these?

**Cutting force:**
Cutting force is functionally determined by cubic material removed per flute. So an increase in tool diameter, stepover, cutting depth, or chipload increases our cutting forces. These are more or less proportional. Although when you include things like a helix in a tool this causes a change in the force direction which can change the when or where a failure point is.

Additional to this there are tool geometry differences that can change our cutting forces. As previously mentioned the helix (flute twist) would be one. In the case of helix the tighter the helix the more force you are redirecting to the Z axis. This can be a good thing if it gives us more room for a higher chipload where we were otherwise listed. However, it can also cause tearout of the material as we are now trying to shear it in a vertical direction.

Another thing that will affect this is the edge radius and the rake. The rake of a tool is the angle of attack. The more positive rake the less force required to take the same chip. The edge radius is the actual thickness of the edge of the tool. It’s a function of the carbide grade, grind, and geometry. The thinner it is the smaller the impact surface of the edge and the less force. Like for like, these both come at the cost of flute strength.


**Surface speed:**
This is a complicated issue. The simple way to put it is that this is the rotational velocity of the edge of the tool as it spins. The larger the tool or the faster the RPM the higher the surface speed. There’s a limit based on the tool and the material where you can damage one or the other. The flip side of this is until that point you increase shear and can get a cleaner cut.

**Acceleration compensation:**
One thing that has to be accounted for in all of this is we can’t keep the chipload up in the entire cut. This is affected by length of the cut and direction changes. Basically we can’t go from 0-60 in zero seconds and we can’t take a 90° turn at full speed. So any changes in direction or short segments of cuts will slow down and speed up based on the machine, controller, and settings. This is something that should at least be considered when thinking about feed rates and tool selection as we want to at least be hitting minimum or as close as we can get the majority of the cut. Basically we want to be cutting above the minimum enough that we have margin for direction changes.

Here is a video discussing bit types
<https://www.youtube.com/watch?v=seAmL6mtqgM>
In general we will go slower than the recommended speeds for large commercial gantry machines. 

This section was also from that forum discussion:
 In the case of the Maslow we are force limited with a stronger resistance in the Z due to the support of the platform on the wood. So let’s use a rule of thumb number for soft material of a 0.002" chipload, the Dewalt 611 RPM range of 16KRPM - 27KRPM, and say we are using a 2 flute 1/8" cutter. This will work out to a feed of 64IPM (1625mm/m) at 16KRPM. Now let’s say that we are cutting through 1/4" material. Can we cut that in a single slotted pass? Maybe, part of it depends on the material and tool. But let’s say that we can’t due to too high of cutting forces. So in this theoretical we don’t want to reduce the chipload (feed) as we are saying the 0.002" is our minimum. So the better option would be to take multiple passes at it. Depending on the tool we will lower the cutting force by about half if we take 2 1/8" passes, and reduce it by half of that if we take 4 1/16" passes. Another option would be to get rid of the slotting by adaptive or even basic pocketing to come out to your edges. This would reduce the load by ~half again if we are taking a 50% stepover.

The above could also be changed by the tool if we have a higher helix upcut as it will drive more of the force into the stronger Z direction. If we have a small enough edge radius, rake, etc. we’ll have a smaller minimum chipload. If we have a higher rake and enough flute relief we will have less force in the same cut.

Also  
"Preface: I’m with PreciseBits. So while I try to only post general information take everything I say with the understanding that I have a bias.

I won’t talk too much about routers as I have a very strong bias in this type towards the 611. But in general the 2 biggest “failure” points are usually brushes and bearings. Brushes are replaceable and cheap. Bearing failure is basically throwing out the router or investing in tools for bearing changes. With that said, I’d stick to a brand name that will likely use at least decent bearings that are spec’ed to the RPM and load expected.

I’m also obviously bias in tooling but can provide a lot more general information here. The biggest limit from my perspective is holding down the cutting forces. So you have a combination of trying to actually “cut” a chip and keep the cutting forces down to keep it “on path”.

The short version of this would be use a 1 or 2 flute cutter, preferably no larger than 1/8", with a high helix (flute twist), and preferably an up-cut. Why?

With the max feed and minimum RPM of the system you can't really support a higher flute count and still "cut". You can still remove material in the shape of the tool. But you will be grinding or rubbing it out of the way. The minimum it takes to actually "cut" material is dependent on the tool geometry and the material. Softer more flexible material will need a higher chipload (feed).

The single flute will have an addition advantage here as it’s not nearly as susceptible to runout (tool spin off the center axis of the router). Runout in multi-flute tools will cause the chipload (feed) to fluctuate across the flutes. Or if the runout is greater than the chipload, push all of the feed onto a single flute (in the case of a 2 flute cutter).

For the same tool geometry cutting forces increase for any increase in tool diameter, chipload (feed), and pass depth. As you are limited by the ability to resist cutting forces the only real reason to go larger than a 1/8" is if you need to cut deeper.

The higher the helix (flute twist) the more force is driven into the “Z”. Keep in mind though that that force is also going to be exerted on the material and that could lead to tear out. Probably the worst thing would be straight flute cutters. They engage the entire length of the flute at the same time and cause large spikes of force.

The up-cut is because that direction will try to force the Maslow through the material it’s cutting. As opposed to a down-cut where the force will be trying to lift the Maslow off the material. So the up-cut especially with a higher helix tool should provide more rigidity.

You should be able to ballpark it with chipload. Like for like any cut that would work with say a single flute tool would work at double the feed with a 2 flute. You won’t have a true like for like though due tool geometry chanes like flute rake, helix, core, etc. But it should get you close for similar geometries.

If anyone doesn’t know the simple version of chipload is the widest part of the chip taken by a single flute in a single flute rotation. So, **Chipload = Feed / RPM / Number of flutes.**"



# Safety
Unplug the router and Maslow while changing bits. 
Router bits are razor sharp and will be burning hot after use.  Consider gloves or have a plan. The impulse to just reach in and grab the working end of the bit is not a good one. Also think about where the bit will fall when the chuck is loosened. Dropping an $80 dollar bit on concrete is sad. 


# BIT LIBRARY 
## To add a Bit start a new entry with a title started by three ### hash symbols then add pictures, materials and description and links. Add to existing entries with your settings and uses.  Still working on what is a useful format here, use your judgment. If we use the heading system built into markdown it will automatically create a table of contents in the top right corner of the reading pane. 


### Example Bit entry heading text
PICTURE
- Overview:
- Links:
- Sources:
- Profile:
- Materials used on:
- Speeds and Settings:
- Details:
- Notes:
- More Pictures:
- Credits:

## Straight Cylinders
### Straight 1/4 inch two flute spiral upcut
PICTURE
-  Excellent all round starter bit. Cylinder with spirals that bring material up to the surface as it cuts.  Upcutting can had a slightly rougher edge as wood fibers are pulled up out of the surface. It cuts a 1/4 kerf (cutting hole) as it goes. Limited in resolution. Avaliable anywhere where routers sold. 
- Links:
- Sources:
  - https://shop.maslowcnc.com/products/1-4-inch-two-flute-up-spiral-router-bit
- Profile:
- Materials used on:
  - Plywood 1/2, 3/4 in
  - OSB (oriented strand board) 1/2, 3/4 in
  - Particle board 1/2, 3/4 in
- Speeds and Settings:
- Details:
- Notes:
  - Can act as a 1/4 in drill but it is not very good for deep holes. Pecking or lifting in and out of the cut while working is slower but it helps keep holes clear and circular. It can move in a small circle to drill larger holes but it would be slower than a dedicated short drill bit. 
- More Pictures:
- Credits:


### Straight 1/8 inch cutting, 1/4 inch router shaft. Two flute upcut spiral
PICTURE
- Excellent all round bit.  Smaller with better resolution than a 1/4 inch bit. Have to move a little slower as it is more fragile. Widely available in router stores. Upcutting can make more splinters than downcutting but do clear the debris better. 
- Links:
- Sources:
- Profile:
- Materials used on:
  - Particle board
  - Plywood
  - OSB (oriented strand board)
- Speeds and Settings:
- Details:
- Notes:
- More Pictures:
- Credits:

## V shapes

## Drill Bits

## Rounded Ends

## Adaptors

## Chuck Replacements

## ???Intersting things, add a category. 
