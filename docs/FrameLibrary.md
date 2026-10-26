
![Maslow+Frame+Drawing_005](../assets/FrameLibrary-assets/Maslow_Frame_Drawing_005.webp)
# Frame Library
![image2_006](../assets/FrameLibrary-assets/image2_006.webp)

# Intro

Maslow4 has to have four solid anchor points to pull on. They can be anchors drilled or glued into a floor, Pins attached to a tilted wall frame, hooks on the ends of spars, or even stakes driven into the ground.


This is a curated list of frame designs. If you have a good one make a branch and add your own at the end of the document then make a pull request to have it added. Add drawings, details and descriptions so that someone else could build your frame. Add links to cnc or .stl 3D printing files if the frame uses those as well as links to the forum if there is discussion of the frame there. 

## Frame calculators
As you plan 


Dlang has made a very useful and cool frame calculator here:
<http://lang.hm/maslow/maslow4_frame.html>


geertdoornbos has made a cool one where you can simulate the movement of the robot:
<https://maslowcnc.nl/frame>

Bar's frame simulator here: 
[Layout Simulator](layout-simulator/) ([documentation](layout-simulator/README.md))

Bar's Calibration point tester:
<https://barboursmith.github.io/Calibration-Simulation/>


These show areas where the Maslow can move accurately and areas where it will start to have trouble. These are determined partly by small angles and high tension near the edges (top edge especially in vertical mode) and partly by the arms carrying the motors of the Maslow bumping into the upright pilars of the maslow when the angle between them gets too small. Belts generally can't be closer than 130 degrees or farther apart than 140 degrees. 

## Frame requirements:
- The Four anchor points have to fit within a 5.5 meter by 5.5 meter square.  It is possible to go bigger with belt extensions which are sticks that attach to the ends of the belts.
- Belts are 14.5 feet long when they are shipped When planning your frame and cutting area . It is also important to make sure for your planned cutting area that the belts have enough length to go all the way across the planned moving space.
- Calibration starts by assuming a rectagular frame with all anchors in the same plane.  Your frame does not have to be a perfect rectangle but if you are having calibration issues this might be a place to adjust. 
- The anchors need to have free space in front of them. The belts and belt ends need to be able to swing freely back and forth without hitting things as the maslow moves around.
  
  ![6E78B686-7B2F-4D6C-9158-38F5AF91A4A1_002](../assets/FrameLibrary-assets/6E78B686-7B2F-4D6C-9158-38F5AF91A4A1_002.webp)

- Anchors can be a bolt, a shoulder bolt, cotter pin, or a quick release pin.Each of Maslow4’s belts terminate with a belt end ring which can be attached to an anchor point. The hole in the end of this part is 10mm or 3/8ths inches and can attach to a 10mm or 3/8ths inch bolt. It’s preferable if the bolt is smooth, but it will still work if the bolt is threaded.
- The anchor pins should be easy to pull in and out or slide the belt anchor off the top of them.
![1000012938_004](../assets/FrameLibrary-assets/1000012938_004.webp)

- The anchor pins should not allow the belt ends to slide up and down vertically or fall off of the top, you want just enough space on the pin for the belt end to freely rotate.
- It is better if the anchor pins are in the same plane with the top of the wasteboard that the material to be cut sits on. Even better than that would be if each was in a plane parallel to the sled base straight out from it's position on the Maslow robot. (if the belts came out perfectly straight from the robot)  Anchors will still work if they are below the wasteboard or up and down a bit individually but it will affect the accuracy of the robot's movements.
  <img width="557" height="524" alt="image" src="../assets/FrameLibrary-assets/image_84d8645a.png" />
(image showing belt anchors lined up with belt heights. credit dlang) 
- The anchors need to be solid and not move in any direction. They will be pulled on by the machine with many newtons or as much as 40 pounds of force.  The anchors should not flex the frame that they are attached to.  The more solid the better.
- The frame should include a wasteboard that can be replaced and cut into underneath the intended cutting area.
- The cutting surface should be fairly stiff and flat. Maslow's sled can ride up and down a flexing or curved board but it will affect the accuracy. It is important that the surface that Maslow4 is connected to not flex under that force. This is more important in the vertical configuration where the stresses due to pulling the belts tight are quite different at the top of the sheet than at the bottom of the sheet. In the horizontal configuration the forces are more similar everywhere.
- The frame should fully support the material to be cut.  How will the material be anchored to the frame? You never want the router bit to hit anything steel.  Brass screws, Alluminum screws, plastic pins, bamboo pins, wooden clamps, double sided tape, carpet friction pad, vacuum tables, steel screws well away from the cutting area all work.
- Frames can lie horizontal all the way up to 20 degrees from vertical. Maslow needs a little weight to pull it against the project
- Frames may need to have room for extra wood around the outside of the cutting area at the level of the cutting surface. For cuts that go right to the edge Maslow will tip over when the sled is not supported.
- You can very carefully measure the distance of your frame pins from each other, and assuming you have made it a nice rectange, enter those positions into the Maslow software directly instead of calibrating. For example you would measure in mm from the left side of one anchor pin to the left side of another. This is entered in the Maslow control program under ?settings?. 
- Belt extensions are freely rotating sticks or machine belt that you could optionally add to the ends of the built in maslow belts in order to make a larger frame with a wider cutting area.  So far people have made belt extensions by adding a metal bar at an anchor where it can freely rotate and then attaching the existing belt to the end of the bar. You would enter this in the Maslow control program under ?settings? directly


## Safety

When attaching Maslow4 to a surface it’s important to ask “How bad would it be if I were to cut through the thing that I am cutting and hit this surface. If the answer is “Very bad” then it’s not a great surface to cut on. Generally there is a spoil board or waste board under the piece of wood being cut which protects the underlying surface, but mistakes can happen.

Consider ventilation, flying debris, people's access to the space, noise, and fire risks in the space where you install your frame. 

  

# FRAME LIBRARY 
## To add a frame start a new entry with a title started by three ### hash symbols then add pictures, materials and description and links.  Still working on what is a useful format here, use your judgment. If we use the heading system built into markdown it will automatically create a table of contents in the top right corner of the reading pane. 


### Example frame entry heading text
PICTURE
- Overview:
- Links:
- Materials:
- Details:
- Notes:
- More Pictures:
- Credits:


## Floor Anchor Systems


### Floor Bolts from Maslow instructions
![1000012945](../assets/FrameLibrary-assets/1000012945.webp)
![C5D99411-E417-4E7D-AE56-26935D64B1D2](../assets/FrameLibrary-assets/C5D99411-E417-4E7D-AE56-26935D64B1D2.webp)


- Overview: Drill holes in a concrete floor, insert threaded anchor sleeves put wasteboard on ground in the middle perhaps on a rubber rug fabric anti slide sheet.
- Links:
-- https://www.maslowcnc.com/attaching-to-the-floor
-- https://www.grainger.com/product/DEWALT-Expansion-Anchor-3-8-16-Thread-30RZ53
- Materials:
- - 4  3/8"-16 exapansion bolts.
  - 4  3/8"-16 1.5 or 2 inches long Hex Cap Screw bolts
  - 8  3/8" fender washers optional
  - 4x8' nonslip rug netting fabric
  - 4x8' wasteboard, could be rigid foam insulation, OSB (oriented strand board, plywood or particle board. 
- Details: Drill holes, insert threaded anchor sleeves, perhaps epoxy them. screw bolts in and out each time to attach the belt ends. 
- Notes:

### 3D printed Anchors glued or bolted to a concrete or wooden floor
### Includes a list of different 3D printable Designs. 
![IMG_1728_002](../assets/FrameLibrary-assets/IMG_1728_002.webp)
![image2_004](../assets/FrameLibrary-assets/image2_004.webp)

- Overview:  Print an anchor with a slot for the belt end and two holes for a pin to go through. Glue, screw or bolt the anchor to the floor frame. These work great with any other frame design. When printing print solid without fill patterns. they need to be strong. Consider three D print strengthing techniques. 
- Links:
  <img width="557" height="524" alt="image" src="../assets/FrameLibrary-assets/image_84d8645a.png" />
  - Dlang's raised anchors: <https://cad.onshape.com/documents/764e57e1318b9953e5986480/w/d0fb021f278db7c3ec637c49/e/b61d6c0eb4e663195972c4e3>
    ![Frogmouth+Anchor+Pic_007](https://github.com/user-attachments/assets/ddd65106-6b65-4550-820e-8eafd1ba99b2)

  - Frogmouth anchors <https://www.maslowcnc.com/s/Frogmouth-Anchor-3D-Print.stl>
    ![Corner+Anchor+Pic_007](https://github.com/user-attachments/assets/93ee1d3d-38b0-410f-91a2-669e9c9e4a0f)

  - Triangle Anchors <https://www.maslowcnc.com/s/Triangle-Anchor-3D-Print.stl>
    ![Screenshot+2025-01-24+at+2 14 52 PM](https://github.com/user-attachments/assets/f87f4d91-b15b-46cb-9887-c107f727d61e)

  - Wall anchors <https://www.maslowcnc.com/s/Wall-Anchor.stl>
  - Andrew_Matthews printed anchors
  -<img width="600" alt="image" src="https://github.com/user-attachments/assets/a6df396c-6e10-45a7-a82b-8ea547828eb4" />

    - CAD:<https://cad.onshape.com/documents/52c6753a65b636197608508b/w/22529bd3a6dcbe9860300aa4/e/f488bdd33c15626c4d663c6d?renderMode=0&uiState=693ce94d8ef17e8212cc7c26> ,
    - STL: <https://github.com/agmatthews/maslowcnc/blob/main/Maslow%20Anchor%20v003.stl> ,
    - 3MF: 3MF file on Github (from Prusa slicer) <https://github.com/agmatthews/maslowcnc/blob/main/Maslow%20Anchor%20v003.3mf>
    - Pins: VCN011-10 with rope, 40mm <https://www.aliexpress.com/item/1005005046638455.html>
    - Knob Bolts: M10 (Head 38mm), 70mm (2pcs) <https://www.aliexpress.com/item/1005007130696268.html>


  
- Materials:
- Details:
- Notes:
- - discussion about printable anchors <https://forums.maslowcnc.com/t/printable-bolt-down-tower-anchors/24612>
  - Another thread: <https://forums.maslowcnc.com/t/easy-to-use-concrete-floor-mounts/24595>
  - Easy to use concrete floor mounts.  Hybrid of concrete anchor and 3D printed <https://forums.maslowcnc.com/t/easy-to-use-concrete-floor-mounts/24595>
- More Pictures:
- Credits:  Dlang, Andrew_Matthews



## Vertical format frames
### Maslow Default Wooden Wall Frame. Can be vertical or horizontal
![Maslow+Frame+Drawing_005](https://github.com/user-attachments/assets/25e1c217-b298-4cd6-a462-24bc66bf6aef)
![E080EB3F-7F0D-49AF-A43D-6353258A6639](https://github.com/user-attachments/assets/1a71778c-4c35-4a5c-b8e1-8ab4a2a4fec5)

- Default well tested frame that holds both the workpiece and anchors well. It is mostly a 2x4 wall with plywood or plastic join plates. Corners can just be a bolt into the wood, bolt into an anchor sleeve, bolt between two pieces of plywood or a 3D printed anchor screwed to the wood. Can be vertical (20degrees) or horizontal
- Links:
  -https://www.maslowcnc.com/wooden-frame
- Material List

  - 2 count 10 ft 2x4s ($12.68 at Home Depot - They don’t seem to have fir, but it would be fine)
  - 5 count 8 ft 2x4s ($3.28 at Home Depot)
  - 1 box of 1 and 5/8ths inch drywall screws ($7.58 at Home Depot)
  - 2 count sheets of ¾ inch plywood
  - 4 count  3/8ths by 6” bolts ($1.43 at Home Depot)
  - 4 count 3/8ths Tee Nuts
  - 4 count 3/8ths Wing Nuts ($1.38 at Home Depot)
- Tools:
  - Drill (With 3/8ths drill bit and fillips screw bit)
  - Saw (Anything that can cut plywood)
  - Carpenters Square
  - Tape Measure
  
- Details:
- Notes:
  - Optional Upgrades
  - A 3D printed belt end anchor for bolts or pins can be screwed to the corner plates
  - You can find a 3d printable file for our quick release anchor point 
  - Add cross bracing with a gate kit to increase frame rigidity
  - Suspend the frame on hooks or pulleys for storage

- More Pictures:
- ![899F88F4-C47C-4E2C-88D3-2D28D01D6D31_002](https://github.com/user-attachments/assets/c26e1881-1cf6-484e-9ab7-2d0195fb51a8)
![42F282D7-5E6A-47E4-BA33-A9E156E9D1E6_002](https://github.com/user-attachments/assets/e5ffd4ea-7840-4805-9687-bb892d6775b0)
![90797204-6F40-4B60-8F33-F2EB10633B80](https://github.com/user-attachments/assets/c5584758-0cb5-458f-8fa9-74e059522045)
![Screenshot+2024-01-09+at+4 21 10+PM](https://github.com/user-attachments/assets/5c6189ce-66c1-47ad-b34a-d74a280b9923)

- Credits: Barbour Smith

### Unistrut channel Metal Frame (could be vertical or horizontal) 
![IMG_3036_008](https://github.com/user-attachments/assets/0439475a-a3b8-4c05-82ef-0114278e841d)

- This steel frame constructed from strut channel is the first frame that we built, but we don’t particularly recommend building it. Here is why:

  - Unistrut is pretty expensive and this frame uses a lot of it so it ends up costing quite a bit to build

  - Steel is pretty springy so the frame ends up being more flexible than a comparable wooden frame

  - It uses a lot of really big 3D printed parts which isn’t so much of a problem if you have a 3D printer, but if you have to pay to have them printed they will be pretty expensive

- That being said, enough people have asked about it and we aren’t interested in hiding any information so here is how we built it.
- You can find files for all of the 3D printed parts in the Not-Shop.
- Our frame is constructed from 10 foot lengths of strut and we used eight of them. We used 64 uni-strut bolts and nuts to hold them together.
- At each junction we joined the uni-strut with either a T or L plate.
  ![11C196E5-83A3-4298-8B8B-235ABAE2C6AA_008](https://github.com/user-attachments/assets/c5816ba2-3ffe-45d5-bf55-200315956930)

- Initially we were using steel ones, but they cost like $11 each so we switched to 3D printed ones which seemed to work just as well.
  ![5D8EFE7B-89FA-4F0B-9470-638F591B1D5D_008](https://github.com/user-attachments/assets/6c26d442-176e-4b5e-b090-812ab20a66ca)

- To attach the spoil board to the unistrut verticals we used six counter-sunk uni-strut bolts and nuts.
  ![C58C479F-9103-42B8-BF8B-B828B4C7E4AD_008](https://github.com/user-attachments/assets/8f71aa0c-95ef-4629-ab96-c7535c26ea75)

- In the corners we used a 3D printed anchor point which slips over the end of the strut.
  ![98DA03F6-1FDD-41A9-9119-6CC54181DF3C_004](https://github.com/user-attachments/assets/8e451207-012c-429b-8263-02f039e63330)

- Initially we only had the central three vertical struts, but we found that the steel had too much flex in that configuration so we added additional vertical struts right next to each anchor point.
  
- To create the legs for the frame to stand up we used three large 3D printed parts and strut. Note that the frame is lifted up off the ground slightly with some extra strut material. This is to let the anchor points on the end slide over the strut there.
  ![63603426-A11C-4E4A-BDD1-F2DFFF8FE30F_008](https://github.com/user-attachments/assets/6350e564-8f1e-49f5-b80e-6c2eca06b3ad)

- The lengths of the two parts making up this leg are 21 and 36 inches long.


- Links:
  - https://www.maslowcnc.com/steel-frame
- Materials:
- Details:
- Notes:
  - Not our favorite frame.  Steel of this dimension flexes too much when the machine is working. 
- More Pictures:
- Credits: Barbour Smith

### Pull-out Vertical Frame
<img width="600" alt="image" src="https://github.com/user-attachments/assets/962dcfad-7901-4690-b96e-5a045f2d345a" />

- Hanging the frame on a wall then pulling it out to get the angle for use. Fun idea if it makes things compact on the wall. 
- Links:
  - <https://forums.maslowcnc.com/t/pull-out-vertical-frame/23904/4>
- Materials:
- Details:
- Notes:
  - Some users have just leaned a frame against a wall
  - Others have hung the frame on the wall then lifted the bottom out on cinderblocks. 
- More Pictures:
- Credits: kyleschoen, Rolf


## Horizontal format frames
### Fast Fold up X frame
<img width="600" alt="image" src="https://github.com/user-attachments/assets/d7981c10-ed09-4036-a546-def237aa7244" />

- A frame that is four beams joined at the center by a hinge style pin with a chain or rope putting tension around the outside edge of the x to make it sturdy and stiff. It is easy to put up and take down, fits on or in a car and folds down to four beams with hardware, a chain, and a wasteboard. 
- Links:
  - https://forums.maslowcnc.com/t/new-fast-fold-up-frame-experiment-for-4-1-updated-with-chain/25044/6
- Materials
  <img width="600" alt="image" src="https://github.com/user-attachments/assets/491a59a9-42d2-471f-aa7c-1272ad2a4aeb" />

  - 4 2x8 by 8’ lumber
  - 8 5/16’’ by 4’’ eyebolts with about an inch eye.
  - 16 Washers for eyebolts
  - 8 5/16’’ nuts
  - 4 1/4’’ 6’’ bolts to hold the chains
  - 4 screw hooks or something for anchors
  - 1 large bolt that fits inside the eyebolt eyes 6’’ long and nut
  - 35 feet of chain I used 155lb load chain and turnbuckle Chain has to be large enough so that the 6” bolts can pass through the links.
  - 1 turnbuckle
  - 1 4x8’ osb sheathing for wasteboard. Could be cut in half
- Tools
  - 5/16 inch drill bit
  - 1/4 in drill bit 6 inches of cutting distance long
  - 1 in drill bit
  - 1 1/4 in drill bit
  - Adjustable wrench
  - Wood rasp to flatten one side of drilled holes.
  - Hammer to encourage bolts
  - Carpenter’s square
  - Pencil
  - Measuring tape
  - Lots of long straight drilling sideways, a drill guide might help
 
    
- Details:
  - <img width="600" alt="image" src="https://github.com/user-attachments/assets/e4614b33-2868-4e62-8aee-ae2ce4936fac" />
  - The hardest part was lining up the eyebolts in each board so that each set of two (four inches apart) was stepped down by 1/4 inch from the set before so that they all would fit together stacked in the center. Picture below
<img width="600" alt="image" src="https://github.com/user-attachments/assets/fc06e435-e309-423a-9aa0-6756f3fda62c" />
<img width="600" alt="image" src="https://github.com/user-attachments/assets/226d5585-5968-43bb-ad04-68aaf5adad06" />
<img width="600" alt="image" src="https://github.com/user-attachments/assets/97edc4c3-2d16-4309-afce-59edb0951255" />
<img width="375" height="499" alt="image" src="https://github.com/user-attachments/assets/f4a06386-3c68-446e-80c1-d77e4f6e59a8" />
<img width="375" height="499" alt="image" src="https://github.com/user-attachments/assets/e0948f3d-9320-4dae-87fe-9dd3ad53a060" />
<img width="375" height="499" alt="image" src="https://github.com/user-attachments/assets/76d6856f-ae78-4133-8c2b-eae7aeb9e2cc" />

  - I had to round the corners so that they would all fit with room to change the angle of the X
  - <img width="375" height="499" alt="image" src="https://github.com/user-attachments/assets/72e0cdcc-2de2-4651-9dd4-3cb99c785cea" />
<img width="600" alt="image" src="https://github.com/user-attachments/assets/b0f59fa1-bf79-4a66-ab0c-3bab94b91527" />
<img width="375" height="499" alt="image" src="https://github.com/user-attachments/assets/affc831f-8236-4b6d-91f9-075400502d65" />
  - It is important to go through BOTH links when anchoring the corners for accuracy. Later I marked the links that I used with marker so that I could find them again.
<img width="600" alt="image" src="https://github.com/user-attachments/assets/9a66b79c-988e-46f3-a4ca-1b90f134ebfe" />
<img width="600" alt="image" src="https://github.com/user-attachments/assets/c8740647-0c47-4cc0-aa03-6bc9d1ce2c15" />
<img width="600" alt="image" src="https://github.com/user-attachments/assets/789c72ca-47de-402e-a902-8cee851cb866" />


- Notes:
  - Looking back at it I wanted to clarify five design assumptions I made that may not be obvious.

  - **Tall beams**. I used 2x8 beams for stiffness in the z direction I based this on engineering standard practice for houses and decks about the same size as the frame. I did not calculate anything, just felt right. It worked well. Not sure what optimal would be.

  - **Moving the chain down the beam towards the center**. Putting the chain at the ends of the beams would maximize stiffness. I moved it down because I liked it aesthetically and to greatly reduce expensive chain and weight. Not sure where the optimal place would be. Felt like two feet canteliever at the ends would still be stiff enough.

  - **Centering the chain holes internally on the beams**. One could bolt the chains to the tops or bottoms of the beams for easier use and construction I felt that it would twist the frame and get in the way of the belts. Again partly aesthetic, I did not try the simpler design. It might work well just to have the chains on top as that is where the forces are applied. 

  - **Spacing the eyebolt pairs wide apart in the center.** This is pretty important for the stiffness of the frame. In the z direction the frame is pretty thin. The stability in this direction is entirely dependent on how widely spaced the connections are between the beams. I am not entirely satisfied with eyebolts spaced 4.5 in apart. I considered one sided strap hinges with a removable pin but could not fit eight of them on a 2x8 and they were more expensive. They would fit on a 2x10. And would be easier to make. I would be interested in other designs for the center. It occurs to me that flat metal plates or bars might work really well and stack nicely.

  - **Chain gauge and strength**. You could probably go with a lot lighter chain saving weight and cost. I have no idea what would be optimal here. I chose these because the bolts I wanted to use fit through the links and because I wanted it to be strong on a human scale as I tossed the frame around. Chain was a LOT stiffer immediately than the ratchet straps. Tight ropes of any sort would work well, be cheap and accessible and light, but I think you would have to recalibrate on each set up. One can also count chain links to ensure rectangularity.
    
  - Originally prototyped this with racheting tie down straps looped around through each beam and it worked well.  The nyolon in the straps is kind of bouncy so I switched for chains.  With the faster calibration times the straps might work ok.  I was impressed by how stiff the frame is with the chains.
  - Next iteration I am goign to move the chains further out on the beams to increase stiffness.
  - Would be easy to scale up with 12 foot beams but you would have to use belt extensions on that I think.
  - I would also redesign the center to make my own hinge out of a stack of mending plates stepped down on each beam instead of the eyebolts. Seems easier and cheaper. <img width="427" height="503" alt="image" src="https://github.com/user-attachments/assets/5c24a79d-35de-415c-b43a-b7e4f8ae4407" />

- More Pictures:
  - Variation using hinges.  I don't recommend this because it was dangerous with pinch points and had to be moved all as one unit.  People have suggested other types of hinges, I would try it with Strap hinge halves so you could just slide a pin in and out of all of them but they did not fit on the 2x8 and were much more expensive. 
- Credits: Wouldchuck

### Shed X-frame
PICTURE
- Anchors in the in inside corners of a shed. Board lifted off the bottom of the shed on an X of beams. Cool use of the structure of a shed to make a smaller, 1/4 sheet,  frame.  Nice Corner anchors and spacing system. 
- Links:
  - https://forums.maslowcnc.com/t/shed-x-frame-what-i-did-and-some-insights/25230/3
- Materials:
- Details:
  - <img width="375" height="500" alt="image" src="https://github.com/user-attachments/assets/bea665b3-7aa7-453c-b411-08520dd9bcdb" />
  - I like this bolt-through-brackets as it lets me play with and adjust the anchor point height with 3D printed shims.
    <img width="600" alt="image" src="https://github.com/user-attachments/assets/4f568437-8d8f-4f4f-bebb-d6cd8aaeef73" />
   - For adjusting the anchor point height, I printed a bunch of spacers - what you see here is far more than needed:
   - Originally, the black ones were going to be for setting the anchor height, and red ones for quick adjustments for material height when experimenting. In the end, the red ones also were a lot of the smaller shim heights.
   - I printed something like 0.5,1,2,4, 6, 8, 9, 12, 18 and 40mm sizes. To get what I wanted I actually did:
   - Use a long straight edge (spirit level, metal edge, etc) that is long enough to lay across the wasteboard and extend all the way to the corner to get a reading on level with the top of the wasteboard.
   - Shim to this height, and repeat for each corner.
   - Look up (or measure in my case of a non-standard Maslow) the height of the bottom of the lowest anchor point.
   - Add shims to this value to that corner. Repeat for each corner based on their arm-heights.
   - I then gathered all the shims for each corner and taped with 50mm tape wrapped round to make a single long shim trimmed to the correct height for each corner.


  - I used a laser level to get all of these vertical. The reason being, my shed is not square, at all, and so if I want to shim but have the positions relative to each other be consistent, vertical with a laser level gives me that.
  -  Bolts are ok, but once you’re happy with it, it’s easy to get hold of steel rod to replace them. Use 9.5mm not 10mm. I also did a little handles on mine:
<img width="600" alt="image" src="https://github.com/user-attachments/assets/867619f5-d234-45bb-90e4-28fb9b075f48" />
  -  The frame. I wanted something that can be packed away, so I did a variant of the X-frame to hold the wood off of the floor:
<img width="600" alt="image" src="https://github.com/user-attachments/assets/92149360-e19d-4141-86eb-3854cfcd9477" />
  -  Did an X-frame even though the attachment points are in the wall, because having a frame to screw things into makes a lot of stuff easier.
  - My waste board is screwed down into the X-frame - this means nothing moves round.
  - But also, I made a while ago1200mm*200mm boards in the thicknesses I tend to cut. These have a dual purpose:
    - They can be offset like this to hold any size workpiece, and screwed through to the frame.
    - They let you cut right to the edge by supporting the Maslow up to it’s edges.
      <img width="471" height="500" alt="image" src="https://github.com/user-attachments/assets/3da3269d-58fe-4416-8350-4ae0bc9830cb" />

  - And that’s about it. Works well for me, is very solid, and can be put away / set up in about 10 mins if needed.

- Notes:
  - So, in the spirit of a recent thread, here’s my 1/4 frame (ie can cut a 1/4 sheet) - and some reasons behind the design decisions.
  - First - sizing. I’m pretty limited to this, it was more a case of picking the work size based on the shed size. I am using anchor points into the studs (more on that shortly), so i only had a bit of wiggle room. Playing around in the frame calculators, I went for ~2500 * 2100. It gives good cutting area for 1220 * 610 (in fact it gives a good cutting area for 1220 * 800).
  - Insight - if you have the freedom, don’t assume longer in one dimension is always better - if you have a limitation in one dimension (shed width in my case) - play with the other dimension when designing, to get the best width/height of the cutting area.
  - Second - the anchor points. Although I am doing what looks like a X-frame ala @wouldchuck’s, i’m not. It’s closer to a lot of what people do with bolts into the floor. what I actually did was brackets screwed into studs with bolts:
- More Pictures:
- Credits:

### Maslow4 Collapsable Horizontal Frame on Sawhorses. 
<img width="600" alt="image" src="https://github.com/user-attachments/assets/17e1cc5f-f91e-4122-a197-ab254d87d832" />
<img width="376" height="500" alt="image" src="https://github.com/user-attachments/assets/6e56ee95-3109-4533-bde6-8b81f3767ed6" />

- Two x's of 2x4 spars inset across beams on sawhorses. Makes a nice table to get to the router on. Flexible design.  Concerns about stiffness. 
- Links:
  - https://forums.maslowcnc.com/t/maslow-4-collapsible-frame-for-horizontal-use/19717/9
  - Onshape Design from Dlang https://cad.onshape.com/documents/20374c29acfc54620a72c6ad/w/195bfd9d55ceaae5efbbcd33/e/4364ee75c5b2cffa6174ced9
- Materials:
- Details:
- Notes:
  -  If you want to really reduce the odds of flexure you can try turning the outrigger ends into a compression chord of a truss. By using ratchet straps that pass through a M4 attachment point (think eye-bolt attached under the outrigger) and connecting the straps back to the frame rails at equal-ish angles, that outrigger will now have the shear properties of a simple truss w/o the need to add extra members. I am yet to test the concept on my existing frame, but I have used this rigging in construction for temporary bracing of long cantilevered posts. Attached below is a screenshot of the concept; green representing the path of each racket strap.
    <img width="600" alt="image" src="https://github.com/user-attachments/assets/940d2280-344b-4d08-a0a6-ac16d6650cf7" />
  - Adding greater shear strength could also be achieved with a bigger outrigger member, a box beam member or shear plates / panels as discussed in the thread. It’s worth noting that many of these concepts are at odds the initial purpose of this design: to make a frame as minimal as possible so it can be assembled, disassembled and moved easily. Granted, everyone has different needs and priorities. Mine just require these somewhat restrictive constraints. I’m interested to see what you all come up with your own constraints and priorities.

- More Pictures:
  <img width="600" alt="image" src="https://github.com/user-attachments/assets/fe8946d0-cdcc-4fef-8c56-87ac000d7768" />
<img width="523" height="784" alt="image" src="https://github.com/user-attachments/assets/d2a1bdcc-461c-4790-8c71-3b170efadf40" />
<img width="509" height="863" alt="image" src="https://github.com/user-attachments/assets/8579849d-2a98-4993-8404-3d4973c897bc" />
<img width="600" alt="image" src="https://github.com/user-attachments/assets/e4cbab2e-ccba-4aad-a0f6-cbd4e8f2c92e" />
<img width="405" height="500" alt="image" src="https://github.com/user-attachments/assets/f0226ffd-eaea-4cfc-9f44-68f110a96241" />
<img width="600" alt="image" src="https://github.com/user-attachments/assets/ba0a0357-1f93-4acb-944c-878e6b387f40" />

- Credits:Garse, Dlang, Rolf, Grim

## Other frame Formats

### Discussion of a rectangle of beams with corners to attach the anchors 
<img width="600" alt="image" src="https://github.com/user-attachments/assets/957371f0-cfde-434d-8bf2-d487f8fb77ef" />

- https://forums.maslowcnc.com/t/possible-super-simple-frame/20306/29
- Notes:
  -Frame and wood would need to be jammed in place or weighted down.

### Discussion of using an angled shelf
- <https://forums.maslowcnc.com/t/maslow-frame-with-storage-shelves/19879>

### Disucsssion of using two two by fours, wasteboard and two ladders
- <https://forums.maslowcnc.com/t/maslow-frame-with-storage-shelves/19879>

### Steel pipe and rope 
<img width="600" alt="image" src="https://github.com/user-attachments/assets/66d07080-c84b-4d25-8752-bf8e22f4edbc" />
<img width="600" alt="image" src="https://github.com/user-attachments/assets/86e4b1fd-f9ec-4d40-9971-7fb66cc47878" />

- Too flexible in the z direction arms would visibly lift up.  Maybe with more structure but the fun of this was that it was huge (too big) and packed up to nothing. 


