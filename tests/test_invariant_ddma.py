import pytest
import ctypes
import struct
import sys
from unittest.mock import MagicMock, patch


# Simulated DMA controller emulation (mirrors the security boundary of ddma.c)
class DMAController:
    """Simulated DMA controller with memory bounds enforcement."""
    
    MAX_TRANSFER_SIZE = 0x10000  # 64KB max transfer
    MEMORY_SIZE = 0x100000       # 1MB emulated memory
    
    def __init__(self, memory_size=None):
        self.memory_size = memory_size or self.MEMORY_SIZE
        self.memory = bytearray(self.memory_size)
        self.base_address = 0
        self.transfer_size = 0
        self.is_initialized = False
    
    def initialize(self):
        """Simulate device initialization (calloc equivalent)."""
        self.memory = bytearray(self.memory_size)
        self.base_address = 0
        self.transfer_size = 0
        self.is_initialized = True
    
    def reset(self):
        """Simulate device reset (free + calloc equivalent)."""
        # Simulate the free(dev) + calloc pattern
        old_memory = self.memory
        del old_memory  # Simulate free
        self.initialize()  # Simulate calloc (zeroed memory)
    
    def program_transfer(self, address, size):
        """
        Program a DMA transfer. Must validate bounds before executing.
        Returns True if transfer is valid, False if it would violate bounds.
        """
        if not self.is_initialized:
            return False
        
        # Security invariant: address and size must be within bounds
        if address < 0:
            return False
        if size < 0:
            return False
        if size > self.MAX_TRANSFER_SIZE:
            return False
        if address >= self.memory_size:
            return False
        if address + size > self.memory_size:
            return False
        # Check for integer overflow
        if address + size < address:  # overflow check
            return False
        
        self.base_address = address
        self.transfer_size = size
        return True
    
    def execute_transfer(self, data):
        """Execute a DMA transfer. Must only write within bounds."""
        if not self.is_initialized:
            raise RuntimeError("Device not initialized")
        
        if self.transfer_size == 0:
            return True
        
        end_addr = self.base_address + self.transfer_size
        
        # Security invariant: never write outside allocated memory
        assert end_addr <= self.memory_size, (
            f"DMA transfer would exceed memory bounds: "
            f"end_addr={end_addr:#x} > memory_size={self.memory_size:#x}"
        )
        assert self.base_address >= 0, "DMA base address must not be negative"
        assert self.transfer_size <= self.MAX_TRANSFER_SIZE, (
            f"Transfer size {self.transfer_size} exceeds maximum {self.MAX_TRANSFER_SIZE}"
        )
        
        write_data = data[:self.transfer_size]
        self.memory[self.base_address:end_addr] = write_data.ljust(self.transfer_size, b'\x00')
        return True
    
    def read_transfer(self, address, size):
        """Execute a DMA read. Must only read within bounds."""
        if not self.is_initialized:
            raise RuntimeError("Device not initialized")
        
        end_addr = address + size
        
        # Security invariant: never read outside allocated memory
        assert end_addr <= self.memory_size, (
            f"DMA read would exceed memory bounds: "
            f"end_addr={end_addr:#x} > memory_size={self.memory_size:#x}"
        )
        assert address >= 0, "DMA read address must not be negative"
        
        return bytes(self.memory[address:end_addr])


# Adversarial payloads: (address, size, description)
ADVERSARIAL_PAYLOADS = [
    # Integer overflow attempts
    {"address": 0xFFFFFFFF, "size": 0x1, "desc": "address near max uint32"},
    {"address": 0xFFFFFFFF, "size": 0xFFFFFFFF, "desc": "both near max uint32 - overflow"},
    {"address": 0x7FFFFFFF, "size": 0x7FFFFFFF, "desc": "signed int max overflow"},
    {"address": 0xFFFFFFFE, "size": 0x2, "desc": "address+size wraps uint32"},
    
    # Out-of-bounds addresses
    {"address": 0x100000, "size": 0x1, "desc": "address exactly at memory limit"},
    {"address": 0x200000, "size": 0x1, "desc": "address beyond memory"},
    {"address": 0x1000000, "size": 0x100, "desc": "far beyond memory"},
    
    # Oversized transfers
    {"address": 0x0, "size": 0x200000, "desc": "transfer larger than memory"},
    {"address": 0x0, "size": 0xFFFFFFFF, "desc": "max size transfer"},
    {"address": 0x0, "size": 0x10001, "desc": "one byte over max transfer size"},
    
    # Negative/underflow values
    {"address": -1, "size": 0x100, "desc": "negative address"},
    {"address": 0x0, "size": -1, "desc": "negative size"},
    {"address": -0x1000, "size": -0x1000, "desc": "both negative"},
    
    # Boundary conditions
    {"address": 0x0, "size": 0x0, "desc": "zero size transfer"},
    {"address": 0x0, "size": 0x10000, "desc": "exactly max transfer size"},
    {"address": 0xFFFF0, "size": 0x10, "desc": "transfer ending at memory boundary"},
    {"address": 0xFFFF0, "size": 0x11, "desc": "transfer one byte past memory boundary"},
    
    # Alignment attacks
    {"address": 0x1, "size": 0xFFFFF, "desc": "unaligned address large transfer"},
    {"address": 0xFFFFF, "size": 0x10000, "desc": "unaligned near-boundary large transfer"},
    
    # Python-specific large integers (simulating 64-bit attacks on 32-bit emulator)
    {"address": 0x100000000, "size": 0x1, "desc": "64-bit address overflow"},
    {"address": 0x0, "size": 0x100000000, "desc": "64-bit size overflow"},
    {"address": sys.maxsize, "size": 0x1, "desc": "sys.maxsize address"},
    {"address": 0x0, "size": sys.maxsize, "desc": "sys.maxsize size"},
    
    # Crafted to bypass naive checks
    {"address": 0x80000, "size": 0x80001, "desc": "address+size just overflows memory"},
    {"address": 0xFFFFF, "size": 0x1, "desc": "last valid byte"},
    {"address": 0xFFFFF, "size": 0x2, "desc": "one byte past last valid byte"},
]


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS, ids=[p["desc"] for p in ADVERSARIAL_PAYLOADS])
def test_dma_bounds_invariant(payload):
    """
    Invariant: DMA controller must NEVER allow transfers that access memory
    outside the allocated emulated memory region, regardless of adversarial
    address/size inputs from a guest OS. After device reset (free+calloc),
    the security boundary must be re-established and enforced.
    """
    address = payload["address"]
    size = payload["size"]
    
    dma = DMAController()
    dma.initialize()
    
    # Test 1: program_transfer must reject out-of-bounds requests
    result = dma.program_transfer(address, size)
    
    if result:
        # If the transfer was accepted, it MUST be within bounds
        assert address >= 0, (
            f"Accepted negative address: {address}"
        )
        assert size >= 0, (
            f"Accepted negative size: {size}"
        )
        assert address < dma.memory_size, (
            f"Accepted address {address:#x} >= memory_size {dma.memory_size:#x}"
        )
        assert size <= dma.MAX_TRANSFER_SIZE, (
            f"Accepted size {size:#x} > MAX_TRANSFER_SIZE {dma.MAX_TRANSFER_SIZE:#x}"
        )
        assert address + size <= dma.memory_size, (
            f"Accepted transfer [{address:#x}, {address+size:#x}) exceeds "
            f"memory_size {dma.memory_size:#x}"
        )
        # Verify no integer overflow
        assert address + size >= address, (
            f"Integer overflow detected: address={address:#x} size={size:#x}"
        )
        
        # Test 2: execute_transfer must succeed without memory corruption
        test_data = b'\xAA' * min(size, 256) if size > 0 else b''
        try:
            dma.execute_transfer(test_data)
        except AssertionError as e:
            pytest.fail(f"execute_transfer violated bounds invariant: {e}")
        
        # Test 3: After reset (free+calloc simulation), bounds must still be enforced
        dma.reset()
        assert dma.is_initialized, "Device must be initialized after reset"
        assert len(dma.memory) == dma.memory_size, (
            "Memory size must be consistent after reset"
        )
        
        # Re-program after reset must still enforce bounds
        result_after_reset = dma.program_transfer(address, size)
        if result_after_reset:
            assert address + size <= dma.memory_size, (
                f"Post-reset: accepted out-of-bounds transfer "
                f"[{address:#x}, {address+size:#x})"
            )
    else:
        # Transfer was rejected - verify it SHOULD have been rejected
        should_reject = (
            address < 0 or
            size < 0 or
            size > dma.MAX_TRANSFER_SIZE or
            address >= dma.memory_size or
            (address >= 0 and size >= 0 and address + size > dma.memory_size) or
            (address >= 0 and size >= 0 and address + size < address)  # overflow
        )
        
        # If it was a valid transfer that got rejected, that's overly conservative
        # but NOT a security violation. We only fail if an invalid transfer was accepted.
        # This branch just confirms rejection happened for a reason.
        if not should_reject:
            # Valid transfer was rejected - this is overly conservative but safe
            # Not a security failure, just a functionality note
            pass


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS, ids=[p["desc"] for p in ADVERSARIAL_PAYLOADS])
def test_dma_no_host_memory_escape_after_reset(payload):
    """
    Invariant: After device reset (simulating free(dev) + calloc pattern),
    the DMA controller must not retain stale state that could be exploited
    to access host memory outside the emulated region.
    """
    address = payload["address"]
    size = payload["size"]
    
    dma = DMAController()
    dma.initialize()
    
    # Attempt to set up a malicious transfer before reset
    dma.program_transfer(address, size)
    
    # Simulate the free(dev) + calloc pattern
    dma.reset()
    
    # After reset, state must be clean
    assert dma.base_address == 0, (
        f"After reset, base_address must be 0, got {dma.base_address:#x}"
    )
    assert dma.transfer_size == 0, (
        f"After reset, transfer_size must be 0, got {dma.transfer_size}"
    )
    assert dma.is_initialized, "Device must be initialized after reset"
    
    # Memory must be zeroed (calloc behavior)
    assert all(b == 0 for b in dma.memory), (
        "After reset (calloc), memory must be zeroed - stale data could leak host info"
    )
    
    # Verify memory region is exactly the expected size
    assert len(dma.memory) == dma.memory_size, (
        f"Memory region size mismatch after reset: "
        f"expected {dma.memory_size}, got {len(dma.memory)}"
    )


@pytest.mark.parametrize("payload", ADVERSARIAL_PAYLOADS, ids=[p["desc"] for p in ADVERSARIAL_PAYLOADS])
def test_dma_read_bounds_invariant(payload):
    """
    Invariant: DMA read operations must never access memory outside the
    allocated emulated memory region, preventing guest-to-host memory disclosure.
    """
    address = payload["address"]
    size = payload["size"]
    
    dma = DMAController()
    dma.initialize()
    
    # Determine if this is a valid read request
    is_valid = (
        isinstance(address, int) and
        isinstance(size, int) and
        address >= 0 and
        size >= 0 and
        size <= dma.MAX_TRANSFER_SIZE and
        address < dma.memory_size and
        address + size <= dma.memory_size and
        address + size >= address  # no overflow
    )
    
    if is_valid and size > 0:
        # Valid reads must succeed without accessing out-of-bounds memory
        try:
            result = dma.read_transfer(address, size)
            assert len(result) == size, (
                f"Read returned {len(result)} bytes, expected {size}"
            )
            assert isinstance(result, bytes), "Read must return bytes"
        except AssertionError as e:
            pytest.fail(f"Valid read violated bounds invariant: {e}")
    else:
        # Invalid reads must be rejected (raise exception or return error)
        # They must NOT silently succeed with out-of-bounds data
        if size > 0 and isinstance(address, int) and isinstance(size, int):
            try:
                # If this doesn't raise, verify it didn't access out-of-bounds
                if address >= 0 and size >= 0:
                    end = address + size
                    if end > dma.memory_size or address >= dma.memory_size:
                        with pytest.raises((AssertionError, IndexError, ValueError, OverflowError)):
                            dma.read_transfer(address, size)
            except (TypeError, OverflowError):
                pass  # Expected for extreme values