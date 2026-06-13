#include "ShaderProgram.hpp"
#include <iostream>
#include "glm/mat4x4.hpp"
#include <glm/gtc/type_ptr.hpp>


ShaderProgram::ShaderProgram(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
{
	GLuint vertexShaderID;
	if (!createShader(textFromfile(vertexShaderPath), GL_VERTEX_SHADER, vertexShaderID)) {
		std::cout << vertexShaderPath << std::endl;
		std::cerr << "VERTEX_SHADER:Compile time error \n" << std::endl;
	}

	GLuint fragmentShaderID;
	if (!createShader(textFromfile(fragmentShaderPath), GL_FRAGMENT_SHADER, fragmentShaderID)) {
		std::cout << fragmentShaderPath << std::endl;
		std::cerr << "FRAGMENT_SHADER:Compile time error \n" << std::endl;
		return;
	}

	m_ID = glCreateProgram();
	glAttachShader(m_ID, vertexShaderID);
	glAttachShader(m_ID, fragmentShaderID);
	glLinkProgram(m_ID);


	GLint success;
	glGetProgramiv(m_ID, GL_LINK_STATUS, &success);
	if (!success)
	{
		GLchar infoLog[1024];
		glGetShaderInfoLog(m_ID, 1024, nullptr, infoLog);
		std::cerr << "ERROR::SHADER:Link time error: \n" << infoLog << std::endl;

	}
	else
	{
		m_isCompiled = true;
	}

	glDeleteShader(vertexShaderID);
	glDeleteShader(fragmentShaderID);
}

ShaderProgram::ShaderProgram(const std::string& computeShaderPath)
{
	GLuint computeShaderID;
	if (!createShader(textFromfile(computeShaderPath), GL_COMPUTE_SHADER, computeShaderID)) {
		std::cout << computeShaderPath << std::endl;
		std::cerr << "COMPUTE_SHADER:Compile time error \n" << std::endl;
		return;
	}

	m_ID = glCreateProgram();
	glAttachShader(m_ID, computeShaderID);
	glLinkProgram(m_ID);

	GLint success;
	glGetProgramiv(m_ID, GL_LINK_STATUS, &success);
	if (!success)
	{
		GLchar infoLog[1024];
		glGetProgramInfoLog(m_ID, 1024, nullptr, infoLog); // ќбратите внимание: glGetProgramInfoLog дл€ программы
		std::cerr << "ERROR::SHADER:Link time error: \n" << infoLog << std::endl;
	}
	else
	{
		m_isCompiled = true;
	}

	// ѕосле линковки сам объект шейдера можно удалить
	glDeleteShader(computeShaderID);
}
bool ShaderProgram::createShader(const std::string& source, const GLenum shaderType, GLuint& shaderID)
{
    shaderID = glCreateShader(shaderType);
    const char* code = source.c_str();
    GLint length = static_cast<GLint>(source.length()); // явно получаем длину строки
    
    glShaderSource(shaderID, 1, &code, &length); 
    glCompileShader(shaderID);

    GLint success;
    glGetShaderiv(shaderID, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        GLchar infoLog[1024];
        glGetShaderInfoLog(shaderID, 1024, nullptr, infoLog);
        std::cerr << "ERROR::SHADER:Compile time error \n" << infoLog << std::endl;
        return false;
    }
    return true;
}

ShaderProgram::~ShaderProgram()
{
	glDeleteProgram(m_ID);
}

void ShaderProgram::use() const
{
	glUseProgram(m_ID);
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& shaderProgram) noexcept
{
	glDeleteProgram(m_ID);
	m_ID = shaderProgram.m_ID;
	m_isCompiled = shaderProgram.m_isCompiled;

	shaderProgram.m_ID = 0;
	shaderProgram.m_isCompiled = false;
	return *this;

}
ShaderProgram::ShaderProgram(ShaderProgram&& shaderProgram) noexcept
{
	glDeleteProgram(m_ID);
	m_ID = shaderProgram.m_ID;
	m_isCompiled = shaderProgram.m_isCompiled;

	shaderProgram.m_ID = 0;
	shaderProgram.m_isCompiled = false;
}

void ShaderProgram::setMatrix4(const char* name, const glm::mat4& matrix) const
{
	glUniformMatrix4fv(glGetUniformLocation(m_ID, name), 1, GL_FALSE, glm::value_ptr(matrix));
}
void ShaderProgram::setVec2(const std::string& name, const glm::vec2& value)
{
	GLint loc = glGetUniformLocation(this->m_ID, name.c_str());
	if (loc != -1)
		glUniform2f(loc, value.x, value.y);
}

void ShaderProgram::setVec3(const std::string& name, const glm::vec3& value)
{
	GLint loc = glGetUniformLocation(this->m_ID, name.c_str());
	if (loc != -1)
		glUniform3f(loc, value.x, value.y, value.z);
}
void ShaderProgram::setVec4(const std::string& name, const glm::vec4& value)
{
	GLint loc = glGetUniformLocation(this->m_ID, name.c_str());
	if (loc != -1)
		glUniform4f(loc, value.x, value.y, value.z,value.w);
}

void ShaderProgram::setInt(const std::string& name, int value)
{
	glUniform1i(glGetUniformLocation(m_ID, name.c_str()), value);
}

void ShaderProgram::setBool(const std::string& name, bool value)
{
	glUniform1i(glGetUniformLocation(m_ID, name.c_str()), value);
}

void ShaderProgram::setFloat(const std::string& name, GLfloat value)
{
	glUniform1f(glGetUniformLocation(m_ID, name.c_str()), value);
}
