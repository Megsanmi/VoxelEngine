#pragma once

#include <glad/glad.h>
#include <string>
#include "glm/mat4x4.hpp"
#include <fstream>
#include <sstream>
#include <iostream>




class ShaderProgram {
public:
	ShaderProgram(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
	ShaderProgram(const std::string& computeShaderPath);

	~ShaderProgram();

	bool isCompiled() const { return m_isCompiled; }
	void use() const;

	ShaderProgram() = delete;
	ShaderProgram(ShaderProgram&) = delete;
	ShaderProgram& operator=(const ShaderProgram&) = delete;
	ShaderProgram& operator=(ShaderProgram&&) noexcept;
	ShaderProgram(ShaderProgram&& shaderProgram) noexcept;
	
	void setMatrix4(const char* name, const glm::mat4& matrix) const;

	void setVec2(const std::string& name, const glm::vec2& value);
	void setVec3(const std::string& name, const glm::vec3& value);
	void setVec4(const std::string& name, const glm::vec4& value);

	void setInt(const std::string& name, int value);
	void setBool(const std::string& name, bool value);
	void setFloat(const std::string& name, GLfloat value);

	std::string textFromfile(std::string path)
	{
		std::ifstream shader_path(path);
		std::string shader = "NO FRAGMENT SHADER";

		if (shader_path.is_open()) {
			std::stringstream buffer;
			buffer << shader_path.rdbuf();
			shader = buffer.str();
			shader_path.close();
		}
		else {
			std::cout << "Cant open file " << path << std::endl;
		}

		return shader;
	}

	GLuint m_ID = 0;
private:
		
	bool createShader(const std::string& source, const GLenum shaderType, GLuint& shaderID);
	bool m_isCompiled = false;

};
