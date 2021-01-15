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

Examples:

![alt text](/RayTracing/out/highres/11_bathroom1_1280_1024.jpg)




# PathTracing 

# VolumetricTracing

