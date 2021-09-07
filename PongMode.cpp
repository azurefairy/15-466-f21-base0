#include "PongMode.hpp"

//for the GL_ERRORS() macro:
#include "gl_errors.hpp"

//for glm::value_ptr() :
#include <glm/gtc/type_ptr.hpp>

#include <random>
#include <stack>

PongMode::PongMode() {
	
	//----- allocate OpenGL resources -----
	{ //vertex buffer:
		glGenBuffers(1, &vertex_buffer);
		//for now, buffer will be un-filled.

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	{ //vertex array mapping buffer for color_texture_program:
		//ask OpenGL to fill vertex_buffer_for_color_texture_program with the name of an unused vertex array object:
		glGenVertexArrays(1, &vertex_buffer_for_color_texture_program);

		//set vertex_buffer_for_color_texture_program as the current vertex array object:
		glBindVertexArray(vertex_buffer_for_color_texture_program);

		//set vertex_buffer as the source of glVertexAttribPointer() commands:
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

		//set up the vertex array object to describe arrays of PongMode::Vertex:
		glVertexAttribPointer(
			color_texture_program.Position_vec4, //attribute
			3, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 0 //offset
		);
		glEnableVertexAttribArray(color_texture_program.Position_vec4);
		//[Note that it is okay to bind a vec3 input to a vec4 attribute -- the w component will be filled with 1.0 automatically]

		glVertexAttribPointer(
			color_texture_program.Color_vec4, //attribute
			4, //size
			GL_UNSIGNED_BYTE, //type
			GL_TRUE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 //offset
		);
		glEnableVertexAttribArray(color_texture_program.Color_vec4);

		glVertexAttribPointer(
			color_texture_program.TexCoord_vec2, //attribute
			2, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 + 4*1 //offset
		);
		glEnableVertexAttribArray(color_texture_program.TexCoord_vec2);

		//done referring to vertex_buffer, so unbind it:
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		//done setting up vertex array object, so unbind it:
		glBindVertexArray(0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	{ //solid white texture:
		//ask OpenGL to fill white_tex with the name of an unused texture object:
		glGenTextures(1, &white_tex);

		//bind that texture object as a GL_TEXTURE_2D-type texture:
		glBindTexture(GL_TEXTURE_2D, white_tex);

		//upload a 1x1 image of solid white to the texture:
		glm::uvec2 size = glm::uvec2(1,1);
		std::vector< glm::u8vec4 > data(size.x*size.y, glm::u8vec4(0xff, 0xff, 0xff, 0xff));
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

		//set filtering and wrapping parameters:
		//(it's a bit silly to mipmap a 1x1 texture, but I'm doing it because you may want to use this code to load different sizes of texture)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		//since texture uses a mipmap and we haven't uploaded one, instruct opengl to make one for us:
		glGenerateMipmap(GL_TEXTURE_2D);

		//Okay, texture uploaded, can unbind it:
		glBindTexture(GL_TEXTURE_2D, 0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	for(int i = 0; i < 8; i++)
		for(int j = 0; j < 8; j++)
		{
			if ((i + j) % 2 == 0)
				bricks[i][j] = block_l;
			else bricks[i][j] = block_r;
		}
}

PongMode::~PongMode() {

	//----- free OpenGL resources -----
	glDeleteBuffers(1, &vertex_buffer);
	vertex_buffer = 0;

	glDeleteVertexArrays(1, &vertex_buffer_for_color_texture_program);
	vertex_buffer_for_color_texture_program = 0;

	glDeleteTextures(1, &white_tex);
	white_tex = 0;
}

bool PongMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	
	// inspired by https://wiki.libsdl.org/SDL_GetKeyboardState
	const Uint8 *state = SDL_GetKeyboardState(NULL);
	lpaddle_vel = (float)(state[SDL_SCANCODE_W]) - (float)(state[SDL_SCANCODE_S]);
	rpaddle_vel = (float)(state[SDL_SCANCODE_UP]) - (float)(state[SDL_SCANCODE_DOWN]);

	return false;
}

void PongMode::update(float elapsed) {

	if(blocks_broken == 64) return;

	//----- paddle update -----

	left_paddle.y += lpaddle_vel * 5.0f * elapsed;
	right_paddle.y += rpaddle_vel * 5.0f * elapsed;

	//clamp paddles to court:
	right_paddle.y = std::max(right_paddle.y, -court_radius.y + paddle_radius.y);
	right_paddle.y = std::min(right_paddle.y,  court_radius.y - paddle_radius.y);

	left_paddle.y = std::max(left_paddle.y, -court_radius.y + paddle_radius.y);
	left_paddle.y = std::min(left_paddle.y,  court_radius.y - paddle_radius.y);

	//----- ball update -----

	//speed of ball doubles every four points:
	float speed_multiplier = 6.0f;

	//velocity cap, though (otherwise ball can pass through paddles):
	speed_multiplier = std::min(speed_multiplier, 10.0f);

	balll += elapsed * speed_multiplier * balll_velocity;
	ballr += elapsed * speed_multiplier * ballr_velocity;

	//---- collision handling ----

	//paddles:
	auto paddle_vs_ball = [this](glm::vec2 const &paddle, glm::vec2 &ball, glm::vec2 &ball_velocity) {
		//compute area of overlap:
		glm::vec2 min = glm::max(paddle - paddle_radius, ball - ball_radius);
		glm::vec2 max = glm::min(paddle + paddle_radius, ball + ball_radius);

		//if no overlap, no collision:
		if (min.x > max.x || min.y > max.y) return;

		if (max.x - min.x > max.y - min.y) {
			//wider overlap in x => bounce in y direction:
			if (ball.y > paddle.y) {
				ball.y = paddle.y + paddle_radius.y + ball_radius.y;
				ball_velocity.y = std::abs(ball_velocity.y);
			} else {
				ball.y = paddle.y - paddle_radius.y - ball_radius.y;
				ball_velocity.y = -std::abs(ball_velocity.y);
			}
		} else {
			//wider overlap in y => bounce in x direction:
			if (ball.x > paddle.x) {
				ball.x = paddle.x + paddle_radius.x + ball_radius.x;
				ball_velocity.x = std::abs(ball_velocity.x);
			} else {
				ball.x = paddle.x - paddle_radius.x - ball_radius.x;
				ball_velocity.x = -std::abs(ball_velocity.x);
			}
			//warp y velocity based on offset from rectangle center:
			float vel = (ball.y - paddle.y) / (paddle_radius.y + ball_radius.y);
			ball_velocity.y = glm::mix(ball_velocity.y, vel, 0.75f);
		}
	};
	paddle_vs_ball(left_paddle, balll, balll_velocity);
	paddle_vs_ball(right_paddle, ballr, ballr_velocity);

	auto brick_vs_ball = [this](int i, int j, glm::vec2 &ball, glm::vec2 &ball_velocity) {
		glm::vec2 centre = brick_loc(i, j);
		//compute area of overlap (slightly reduce brick size when detecting collisions):
		glm::vec2 min = glm::max(centre - (block_radius * 0.95f), ball - ball_radius);
		glm::vec2 max = glm::min(centre + (block_radius * 0.95f), ball + ball_radius);

		//if no overlap, no collision:
		if (min.x > max.x || min.y > max.y) return false;

		if (max.x - min.x > max.y - min.y) {
			//wider overlap in x => bounce in y direction:
			if (ball.y > centre.y) {
				ball.y = centre.y + block_radius.y + ball_radius.y;
				ball_velocity.y = std::abs(ball_velocity.y);
			} else {
				ball.y = centre.y - block_radius.y - ball_radius.y;
				ball_velocity.y = -std::abs(ball_velocity.y);
			}
		} else {
			//wider overlap in y => bounce in x direction:
			if (ball.x > centre.x) {
				ball.x = centre.x + block_radius.x + ball_radius.x;
				ball_velocity.x = std::abs(ball_velocity.x);
			} else {
				ball.x = centre.x - block_radius.x - ball_radius.x;
				ball_velocity.x = -std::abs(ball_velocity.x);
			}
		}
		return true;
	};

	for(int i = 0; i < 8; i++){
		for(int j = 0; j < 8; j++){
			if (bricks[i][j] == block_l && brick_vs_ball(i, j, balll, balll_velocity)){
				score++;
				blocks_broken++;
				bricks[i][j] = block_todelete;
			}
			else if (bricks[i][j] == block_r && brick_vs_ball(i, j, ballr, ballr_velocity)){
				score++;
				blocks_broken++;
				bricks[i][j] = block_todelete;
			}
		}
	}
	for(int i = 0; i < 8; i++){
		for(int j = 0; j < 8; j++){
			if (bricks[i][j] != block_empty) {
				brick_vs_ball(i, j, balll, balll_velocity);
				brick_vs_ball(i, j, ballr, ballr_velocity);
			}
			if (bricks[i][j] == block_todelete)
				bricks[i][j] = block_empty;
		}
	}

	//court walls:
	if (balll.y > court_radius.y - ball_radius.y) {
		balll.y = court_radius.y - ball_radius.y;
		if (balll_velocity.y > 0.0f) {
			balll_velocity.y = -balll_velocity.y;
		}
	}
	if (balll.y < -court_radius.y + ball_radius.y) {
		balll.y = -court_radius.y + ball_radius.y;
		if (balll_velocity.y < 0.0f) {
			balll_velocity.y = -balll_velocity.y;
		}
	}

	if (balll.x > court_radius.x - ball_radius.x) {
		balll.x = court_radius.x - ball_radius.x;
		if (balll_velocity.x > 0.0f) {
			balll_velocity.x = -balll_velocity.x;
		}
	}
	if (balll.x < -court_radius.x + ball_radius.x) {
		balll.x = -court_radius.x + ball_radius.x;
		if (balll_velocity.x < 0.0f) {
			balll_velocity.x = -balll_velocity.x;
			score -= 1;
		}
	}

	
	if (ballr.y > court_radius.y - ball_radius.y) {
		ballr.y = court_radius.y - ball_radius.y;
		if (ballr_velocity.y > 0.0f) {
			ballr_velocity.y = -ballr_velocity.y;
		}
	}
	if (ballr.y < -court_radius.y + ball_radius.y) {
		ballr.y = -court_radius.y + ball_radius.y;
		if (ballr_velocity.y < 0.0f) {
			ballr_velocity.y = -ballr_velocity.y;
		}
	}

	if (ballr.x > court_radius.x - ball_radius.x) {
		ballr.x = court_radius.x - ball_radius.x;
		if (ballr_velocity.x > 0.0f) {
			ballr_velocity.x = -ballr_velocity.x;
		}
	}
	if (ballr.x < -court_radius.x + ball_radius.x) {
		ballr.x = -court_radius.x + ball_radius.x;
		if (ballr_velocity.x < 0.0f) {
			ballr_velocity.x = -ballr_velocity.x;
			score -= 1;
		}
	}
	
	if(score < 0) score = 0;
}

void PongMode::draw(glm::uvec2 const &drawable_size) {
	//some nice colors from the course web page:
	#define HEX_TO_U8VEC4( HX ) (glm::u8vec4( (HX >> 24) & 0xff, (HX >> 16) & 0xff, (HX >> 8) & 0xff, (HX) & 0xff ))
	const glm::u8vec4 bg_color = HEX_TO_U8VEC4(0x193b59ff);
	const glm::u8vec4 fg_color = HEX_TO_U8VEC4(0xf2d2b6ff);
	const glm::u8vec4 shadow_color = HEX_TO_U8VEC4(0xf2ad94ff);
	const glm::u8vec4 lcolor = HEX_TO_U8VEC4(0x95ce44ff);
	const glm::u8vec4 lshadow = HEX_TO_U8VEC4(0x6d9d2aff);
	const glm::u8vec4 rcolor = HEX_TO_U8VEC4(0xb7ebf1ff);
	const glm::u8vec4 rshadow = HEX_TO_U8VEC4(0x99e3ebff);
	const glm::u8vec4 shade = HEX_TO_U8VEC4(0x00000040);
	#undef HEX_TO_U8VEC4

	//other useful drawing constants:
	const float wall_radius = 0.05f;
	const float shadow_offset = 0.07f;
	const float padding = 0.14f; //padding between outside of walls and edge of window

	//---- compute vertices to draw ----

	//vertices will be accumulated into this list and then uploaded+drawn at the end of this function:
	std::vector< Vertex > vertices;

	//inline helper function for rectangle drawing:
	auto draw_rectangle = [&vertices](glm::vec2 const &center, glm::vec2 const &radius, glm::u8vec4 const &color) {
		//draw rectangle as two CCW-oriented triangles:
		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));

		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
	};

	//shadows for everything (except the trail):

	glm::vec2 s = glm::vec2(0.0f,-shadow_offset);

	draw_rectangle(glm::vec2(-court_radius.x-wall_radius, 0.0f)+s, glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), shadow_color);
	draw_rectangle(glm::vec2( court_radius.x+wall_radius, 0.0f)+s, glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), shadow_color);
	draw_rectangle(glm::vec2( 0.0f,-court_radius.y-wall_radius)+s, glm::vec2(court_radius.x, wall_radius), shadow_color);
	draw_rectangle(glm::vec2( 0.0f, court_radius.y+wall_radius)+s, glm::vec2(court_radius.x, wall_radius), shadow_color);
	draw_rectangle(left_paddle+s, paddle_radius, lshadow);
	draw_rectangle(right_paddle+s, paddle_radius, rshadow);
	draw_rectangle(balll+s, ball_radius, lshadow);
	draw_rectangle(ballr+s, ball_radius, rshadow);
	for(int i = 0; i < 8; i++)
		for(int j = 0; j < 8; j++)
		{
			switch(bricks[i][j])
			{
				case block_l:
					draw_rectangle(brick_loc(i, j) + s, block_radius, lshadow);
					break;
				case block_r:
					draw_rectangle(brick_loc(i, j) + s, block_radius, rshadow);
					break;
				default:
					break;
			}
		}

	//solid objects:

	//walls:
	draw_rectangle(glm::vec2(-court_radius.x-wall_radius, 0.0f), glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), fg_color);
	draw_rectangle(glm::vec2( court_radius.x+wall_radius, 0.0f), glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), fg_color);
	draw_rectangle(glm::vec2( 0.0f,-court_radius.y-wall_radius), glm::vec2(court_radius.x, wall_radius), fg_color);
	draw_rectangle(glm::vec2( 0.0f, court_radius.y+wall_radius), glm::vec2(court_radius.x, wall_radius), fg_color);

	//paddles:
	draw_rectangle(left_paddle, paddle_radius, lcolor);
	draw_rectangle(right_paddle, paddle_radius, rcolor);
	

	//ball:
	draw_rectangle(balll, ball_radius, lcolor);
	draw_rectangle(ballr, ball_radius, rcolor);

	//blocks:
	for(int i = 0; i < 8; i++)
		for(int j = 0; j < 8; j++) {
			switch(bricks[i][j]) {
				case block_l:
					draw_rectangle(brick_loc(i, j), block_radius, lcolor);
					break;
				case block_r:
					draw_rectangle(brick_loc(i, j), block_radius, rcolor);
					break;
				default:
					break;
			}
		}

	//scores:
	glm::vec2 score_radius = glm::vec2(0.07f, 0.1f);
	for (int i = 0; i < score; ++i){
		draw_rectangle(glm::vec2( -court_radius.x + (2.0f + 2.0f * i) * (score_radius.x + 0.02f), court_radius.y + 2.0f * wall_radius + 2.0f * score_radius.y), score_radius, fg_color);
	}

	if(blocks_broken == 64){
		draw_rectangle(glm::vec2(0.f, 0.f), court_radius + 2.f * wall_radius, shade);
		int columns = (score + 1) / 10;
		for(int i = 0; i < columns; i++)
			for(int j = 0; j < 10 && 10*i + j < score; j++){
				draw_rectangle(glm::vec2(4.0f * (j - 5) * (score_radius.x + 0.03f), 4.0f * (i - columns/2.f) * (score_radius.y + 0.05f)), 2.f * score_radius, fg_color);
			}
	}

	//------ compute court-to-window transform ------

	//compute area that should be visible:
	glm::vec2 scene_min = glm::vec2(
		-court_radius.x - 2.0f * wall_radius - padding,
		-court_radius.y - 2.0f * wall_radius - padding
	);
	glm::vec2 scene_max = glm::vec2(
		court_radius.x + 2.0f * wall_radius + padding,
		court_radius.y + 2.0f * wall_radius + padding
	);

	//compute window aspect ratio:
	float aspect = drawable_size.x / float(drawable_size.y);
	//we'll scale the x coordinate by 1.0 / aspect to make sure things stay square.

	//compute scale factor for court given that...
	float scale = std::min(
		(2.0f * aspect) / (scene_max.x - scene_min.x), //... x must fit in [-aspect,aspect] ...
		(2.0f) / (scene_max.y - scene_min.y) //... y must fit in [-1,1].
	);

	glm::vec2 center = 0.5f * (scene_max + scene_min);

	//build matrix that scales and translates appropriately:
	glm::mat4 court_to_clip = glm::mat4(
		glm::vec4(scale / aspect, 0.0f, 0.0f, 0.0f),
		glm::vec4(0.0f, scale, 0.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
		glm::vec4(-center.x * (scale / aspect), -center.y * scale, 0.0f, 1.0f)
	);
	//NOTE: glm matrices are specified in *Column-Major* order,
	// so each line above is specifying a *column* of the matrix(!)

	//also build the matrix that takes clip coordinates to court coordinates (used for mouse handling):
	clip_to_court = glm::mat3x2(
		glm::vec2(aspect / scale, 0.0f),
		glm::vec2(0.0f, 1.0f / scale),
		glm::vec2(center.x, center.y)
	);

	//---- actual drawing ----

	//clear the color buffer:
	glClearColor(bg_color.r / 255.0f, bg_color.g / 255.0f, bg_color.b / 255.0f, bg_color.a / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	//use alpha blending:
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//don't use the depth test:
	glDisable(GL_DEPTH_TEST);

	//upload vertices to vertex_buffer:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertices[0]), vertices.data(), GL_STREAM_DRAW); //upload vertices array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//set color_texture_program as current program:
	glUseProgram(color_texture_program.program);

	//upload OBJECT_TO_CLIP to the proper uniform location:
	glUniformMatrix4fv(color_texture_program.OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(court_to_clip));

	//use the mapping vertex_buffer_for_color_texture_program to fetch vertex data:
	glBindVertexArray(vertex_buffer_for_color_texture_program);

	//bind the solid white texture to location zero so things will be drawn just with their colors:
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, white_tex);

	//run the OpenGL pipeline:
	glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));

	//unbind the solid white texture:
	glBindTexture(GL_TEXTURE_2D, 0);

	//reset vertex array to none:
	glBindVertexArray(0);

	//reset current program to none:
	glUseProgram(0);
	

	GL_ERRORS(); //PARANOIA: print errors just in case we did something wrong.

}

inline glm::vec2 PongMode::brick_loc(int i, int j){
	return glm::vec2(1 + 2 * j * block_radius.x, 2 * (i-3.5) * block_radius.y);
}