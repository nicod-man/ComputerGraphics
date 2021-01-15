# RayTracing-PathTracing-VolumetricTracing

Fundamentals of Computer Graphics (FoGC): MARR course held by the professor Fabio Pellacini

<a href="https://www.dis.uniroma1.it/"><img src="http://www.dis.uniroma1.it/sites/default/files/marchio%20logo%20eng%20jpg.jpg" width="500"></a>

The course is organized in three homeworks, respectively RayTracing, PathTracing and VolumetricTracing; each one has some extra-credit to implement:

	- RayTracing:  	
			a) Toon Shader;
			b) Refraction;

	- PathTracing: 
			a) rendering some large scene at high resolution,  namely 1280x720 with 1024 samples per pixel;
			b) custom scenes;
			c) refraction;
			d) Stratified Sampling;


	- VolumetricTracing: 
			a) Adaptive Rendering;
			b) Delta-Tracking (for heterogeneous volume);



# RayTracing

In 3D computer graphics, ray tracing is a rendering technique for generating an image by tracing the path of light as pixels in an image plane and simulating the effects of its encounters with virtual objects. The technique is capable of producing a high degree of visual realism, more so than typical scanline rendering methods, but at a greater computational cost. This makes ray tracing best suited for applications where taking a relatively long time to render can be tolerated, such as in still computer-generated images, and film and television visual effects(VFX), but generally more poorly suited to real-time applications such as video games, where speed is critical in rendering each frame. In recent years, however,Hardware acceleration for real-time ray tracing has become standard on new commercial graphics cards, and graphics APIs have followed suit, allowing developers to add real-time ray tracing techniques to games and other real-time rendered media with a lesser, albeit still substantial hit to frame render times.
Ray tracing is capable of simulating a variety of optical effects, such as reflection and refraction, scattering, and dispersion phenomena (such as chromatic aberration). It can also be used to trace the path of sound waves in a similar fashion to light waves, making it a viable option for more immersive sound design in videogames by rendering realistic reverberation and echoes. In fact, any physical wave or particle phenomenon with approximately linear motion can be simulated with these techniques.

Results:

![Random](https://github.com/nicod-man/ComputerGraphics/blob/master/Raytracing/out/out/highres/10_hair_1280_1024.jpg)

Refraction 
![Refraction](https://github.com/nicod-man/ComputerGraphics/blob/master/Raytracing/out/out/lowres/14_refract_1024_256.jpg)

Toon Shader
![ToonShader](https://github.com/nicod-man/ComputerGraphics/blob/master/Raytracing/out/out/lowres/13_toon_1024_256.jpg)



# PathTracing 

Path tracing is a computer graphics Monte Carlo method of rendering images of three-dimensional scenes such that the global illumination is faithful to reality. Fundamentally, the algorithm is integrating over all the illuminance arriving to a single point on the surface of an object. This illuminance is then reduced by a surface reflectance function (BRDF) to determine how much of it will go towards the viewpoint camera. This integration procedure is repeated for every pixel in the output image. When combined with physically accurate models of surfaces, accurate models of real light sources (light bulbs), and optically correct cameras, path tracing can produce still images that are indistinguishable from photographs.

Path tracing naturally simulates many effects that have to be specifically added to other methods (conventional ray tracing or scanline rendering), such as soft shadows, depth of field, motion blur, caustics, ambient occlusion, and indirect lighting. Implementation of a renderer including these effects is correspondingly simpler. An extended version of the algorithm is realized by volumetric path tracing, which considers the light scattering of a scene.

Due to its accuracy and unbiased nature, path tracing is used to generate reference images when testing the quality of other rendering algorithms. In order to get high quality images from path tracing, a very large number of rays must be traced to avoid visible noisy artifacts.

Results:

Large Scenes (High res)
![HighRes](https://github.com/nicod-man/ComputerGraphics/blob/master/PathTracing/out/EXTRACREDIT_IMAGES/large_scenes/bistroexterior_1024_1280.jpg)

![HighRes2](https://github.com/nicod-man/ComputerGraphics/blob/master/PathTracing/out/EXTRACREDIT_IMAGES/large_scenes/sanmiguel_1024_1280.jpg)

Refraction
![Refraction](https://github.com/nicod-man/ComputerGraphics/blob/master/PathTracing/out/EXTRACREDIT_IMAGES/refract/refract_path_2.jpg)

Custom scene (MYOS- My own scenes)
![Custom](https://github.com/nicod-man/ComputerGraphics/blob/master/PathTracing/out/EXTRACREDIT_IMAGES/MYOS/custom_05/custom_05_1024_1280.jpg)

Stratified Sampling (respectively: pure random, uniform, and jittered with 64 samples per pixel)
![Random](https://github.com/nicod-man/ComputerGraphics/blob/master/PathTracing/out/EXTRACREDIT_IMAGES/stratified_sampling/64_normal.jpg)

![Uniform](https://github.com/nicod-man/ComputerGraphics/blob/master/PathTracing/out/EXTRACREDIT_IMAGES/stratified_sampling/64_stratified.jpg)

![Jittered](https://github.com/nicod-man/ComputerGraphics/blob/master/PathTracing/out/EXTRACREDIT_IMAGES/stratified_sampling/64_jittered.jpg)



# VolumetricTracing

The Volumetric shader has the same structure of the Path shader with the addition of handling volumes (both homogenous and heterogenous ones). A brief report explains the handling of the adaptive rendering and the heterogenous volumes.

Results:
![Skin](https://github.com/nicod-man/ComputerGraphics/blob/master/VolumetricTracing/out/out/lowres/04_head1_720_256.jpg)

![AR](https://github.com/nicod-man/ComputerGraphics/blob/master/VolumetricTracing/out/out/adaptive_rendering/adaptive_min_256minsamples_2e-3.jpg)

![HV](https://github.com/nicod-man/ComputerGraphics/blob/master/VolumetricTracing/out/out/delta_tracking/cloud_256_perlin_provasize1.jpg)

![HV2](https://github.com/nicod-man/ComputerGraphics/blob/master/VolumetricTracing/out/out/delta_tracking/cubes_256_perlin_size1.jpg)
