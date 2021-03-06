
#include "r_compiler/llvm_include.h"
#include "ssa_vec16ub.h"
#include "ssa_vec8s.h"
#include "ssa_vec4i.h"
#include "ssa_scope.h"

SSAVec16ub::SSAVec16ub()
: v(0)
{
}

SSAVec16ub::SSAVec16ub(unsigned char constant)
: v(0)
{
	std::vector<llvm::Constant*> constants;
	constants.resize(16, llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant, false)));
	v = llvm::ConstantVector::get(constants);
}

SSAVec16ub::SSAVec16ub(
	unsigned char constant0, unsigned char constant1, unsigned char constant2, unsigned char constant3, unsigned char constant4, unsigned char constant5, unsigned char constant6, unsigned char constant7,
	unsigned char constant8, unsigned char constant9, unsigned char constant10, unsigned char constant11, unsigned char constant12, unsigned char constant13, unsigned char constant14, unsigned char constant15)
: v(0)
{
	std::vector<llvm::Constant*> constants;
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant0, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant1, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant2, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant3, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant4, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant5, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant6, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant7, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant8, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant9, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant10, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant11, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant12, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant13, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant14, false)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(8, constant15, false)));
	v = llvm::ConstantVector::get(constants);
}

SSAVec16ub::SSAVec16ub(llvm::Value *v)
: v(v)
{
}

SSAVec16ub::SSAVec16ub(SSAVec8s s0, SSAVec8s s1)
: v(0)
{
	llvm::Value *values[2] = { s0.v, s1.v };
	v = SSAScope::builder().CreateCall(SSAScope::intrinsic(llvm::Intrinsic::x86_sse2_packuswb_128), values, SSAScope::hint());
}

llvm::Type *SSAVec16ub::llvm_type()
{
	return llvm::VectorType::get(llvm::Type::getInt8Ty(SSAScope::context()), 16);
}

SSAVec16ub SSAVec16ub::bitcast(SSAVec4i i32)
{
	return SSAVec16ub::from_llvm(SSAScope::builder().CreateBitCast(i32.v, llvm_type(), SSAScope::hint()));
}

SSAVec16ub SSAVec16ub::shuffle(const SSAVec16ub &i0, int index0, int index1, int index2, int index3, int index4, int index5, int index6, int index7, int index8, int index9, int index10, int index11, int index12, int index13, int index14, int index15)
{
	return shuffle(i0, from_llvm(llvm::UndefValue::get(llvm_type())), index0, index1, index2, index3, index4, index5, index6, index7, index8, index9, index10, index11, index12, index13, index14, index15);
}

SSAVec16ub SSAVec16ub::shuffle(const SSAVec16ub &i0, const SSAVec16ub &i1, int index0, int index1, int index2, int index3, int index4, int index5, int index6, int index7, int index8, int index9, int index10, int index11, int index12, int index13, int index14, int index15)
{
	std::vector<llvm::Constant*> constants;
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index0)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index1)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index2)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index3)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index4)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index5)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index6)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index7)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index8)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index9)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index10)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index11)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index12)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index13)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index14)));
	constants.push_back(llvm::ConstantInt::get(SSAScope::context(), llvm::APInt(32, index15)));
	llvm::Value *mask = llvm::ConstantVector::get(constants);
	return SSAVec16ub::from_llvm(SSAScope::builder().CreateShuffleVector(i0.v, i1.v, mask, SSAScope::hint()));
}

SSAVec16ub operator+(const SSAVec16ub &a, const SSAVec16ub &b)
{
	return SSAVec16ub::from_llvm(SSAScope::builder().CreateAdd(a.v, b.v, SSAScope::hint()));
}

SSAVec16ub operator-(const SSAVec16ub &a, const SSAVec16ub &b)
{
	return SSAVec16ub::from_llvm(SSAScope::builder().CreateSub(a.v, b.v, SSAScope::hint()));
}

SSAVec16ub operator*(const SSAVec16ub &a, const SSAVec16ub &b)
{
	return SSAVec16ub::from_llvm(SSAScope::builder().CreateMul(a.v, b.v, SSAScope::hint()));
}
/*
SSAVec16ub operator/(const SSAVec16ub &a, const SSAVec16ub &b)
{
	return SSAScope::builder().CreateDiv(a.v, b.v, SSAScope::hint());
}
*/
SSAVec16ub operator+(unsigned char a, const SSAVec16ub &b)
{
	return SSAVec16ub(a) + b;
}

SSAVec16ub operator-(unsigned char a, const SSAVec16ub &b)
{
	return SSAVec16ub(a) - b;
}

SSAVec16ub operator*(unsigned char a, const SSAVec16ub &b)
{
	return SSAVec16ub(a) * b;
}
/*
SSAVec16ub operator/(unsigned char a, const SSAVec16ub &b)
{
	return SSAVec16ub(a) / b;
}
*/
SSAVec16ub operator+(const SSAVec16ub &a, unsigned char b)
{
	return a + SSAVec16ub(b);
}

SSAVec16ub operator-(const SSAVec16ub &a, unsigned char b)
{
	return a - SSAVec16ub(b);
}

SSAVec16ub operator*(const SSAVec16ub &a, unsigned char b)
{
	return a * SSAVec16ub(b);
}
/*
SSAVec16ub operator/(const SSAVec16ub &a, unsigned char b)
{
	return a / SSAVec16ub(b);
}
*/