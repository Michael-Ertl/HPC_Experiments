#include "allocators.h"

// Failure stub.

Allocator::Block Allocator::FailureStub::allocate(size_t size) {
	LOG_ERROR("Failure stub method allocate() has been called.");
	return {};
}

Allocator::Block Allocator::FailureStub::allocateAll() {
	LOG_ERROR("Failure stub method allocateAll() has been called.");
	return {};
}

bool Allocator::FailureStub::expand(Allocator::Block &block, size_t delta) {
	LOG_ERROR("Failure stub method expand() has been called.");
	return false;
}

void Allocator::FailureStub::reallocate(Allocator::Block &block, size_t newSize) {
	LOG_ERROR("Failure stub method reallocate() has been called.");
}

bool Allocator::FailureStub::owns(Allocator::Block block) {
	LOG_ERROR("Failure stub method owns() has been called.");
	return false;
}

void Allocator::FailureStub::deallocate(Allocator::Block block) {
	DEALLOCATE_NULL_CHECK(block);
	LOG_ERROR("Failure stub method deallocate() has been called.");
}

void Allocator::FailureStub::deallocateAll() {
	LOG_ERROR("Failure stub method deallocateAll() has been called.");
}

// Malloc.

Allocator::Block Allocator::Malloc::allocate(size_t size) { 
	return { (u8 *)malloc(size), size };
}

void Allocator::Malloc::reallocate(Allocator::Block &block, size_t newSize) {
	block.ptr = (u8 *)realloc(block.ptr, newSize);
	block.size = newSize;
}

bool Allocator::Malloc::owns(Allocator::Block block) {
	return true;
}

void Allocator::Malloc::deallocate(Allocator::Block block) {
	DEALLOCATE_NULL_CHECK(block);
	free(block.ptr);
}

bool Allocator::Malloc::expand(Block &block, size_t delta) {
	return false;
}


// ElectricFence.

// Instrument.

