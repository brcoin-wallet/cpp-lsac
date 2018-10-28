#pragma once

#include <llvm/IR/IRBuilder.h>

#include <libdevcore/Common.h>

namespace evmcc
{
class GasMeter;

class Memory
{
public:
	Memory(llvm::IRBuilder<>& _builder, llvm::Module* _module, GasMeter& _gasMeter);
	Memory(const Memory&) = delete;
	void operator=(Memory) = delete;

	llvm::Value* loadWord(llvm::Value* _addr);
	void storeWord(llvm::Value* _addr, llvm::Value* _word);
	void storeByte(llvm::Value* _addr, llvm::Value* _byte);
	llvm::Value* getSize();

	void dump(uint64_t _begin, uint64_t _end = 0);

private:
	llvm::Function* createFunc(bool _isStore, llvm::Type* _type, llvm::Module* _module, GasMeter& _gasMeter);

private:
	llvm::IRBuilder<>& m_builder;

	llvm::GlobalVariable* m_data;
	llvm::GlobalVariable* m_size;

	llvm::Function* m_loadWord;
	llvm::Function* m_storeWord;
	llvm::Function* m_storeByte;
	llvm::Function* m_resize;

	llvm::Function* m_memRequire;
	llvm::Function* m_memDump;
	llvm::Function* m_memSize;
};

}