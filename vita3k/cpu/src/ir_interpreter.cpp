// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
#include <cpu/ir_interpreter.h>

#include <dynarmic/frontend/A32/a32_location_descriptor.h>
#include <dynarmic/frontend/A32/translate/a32_translate.h>
#include <dynarmic/frontend/A32/translate/translate_callbacks.h>
#include <dynarmic/interface/A32/config.h>
#include <dynarmic/ir/basic_block.h>
#include <dynarmic/ir/cond.h>
#include <dynarmic/ir/opcodes.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <stdexcept>
#include <unordered_map>

namespace cpu {
namespace {
using namespace Dynarmic;
using Op = IR::Opcode;

bool condition(IR::Cond cond, uint32_t flags) {
    const bool n = flags >> 31, z = (flags >> 30) & 1, c = (flags >> 29) & 1, v = (flags >> 28) & 1;
    switch (cond) {
    case IR::Cond::EQ: return z;
    case IR::Cond::NE: return !z;
    case IR::Cond::CS: return c;
    case IR::Cond::CC: return !c;
    case IR::Cond::MI: return n;
    case IR::Cond::PL: return !n;
    case IR::Cond::VS: return v;
    case IR::Cond::VC: return !v;
    case IR::Cond::HI: return c && !z;
    case IR::Cond::LS: return !c || z;
    case IR::Cond::GE: return n == v;
    case IR::Cond::LT: return n != v;
    case IR::Cond::GT: return !z && n == v;
    case IR::Cond::LE: return z || n != v;
    case IR::Cond::AL: return true;
    default: return false;
    }
}
struct Value {
    uint64_t bits = 0;
    uint32_t flags = 0;
    bool carry = false, overflow = false;
    Value() = default;
    Value(uint64_t value)
        : bits(value)
        , flags((static_cast<uint32_t>(value) & 0x80000000) | (value == 0 ? 0x40000000 : 0)) {}
};
} // namespace

struct IRInterpreter::Impl : A32::TranslateCallbacks {
    State &s;
    Read read;
    Write write;
    std::string failure;
    std::unordered_map<const IR::Inst *, Value> values;
    A32::LocationDescriptor end{ 0, A32::PSR{ 0 }, A32::FPSCR{ 0 } };
    bool check_bit = false;

    Impl(State &state, Read read_, Write write_)
        : s(state)
        , read(std::move(read_))
        , write(std::move(write_)) {}
    std::optional<uint32_t> MemoryReadCode(uint32_t address) override { return static_cast<uint32_t>(read(address, 4)); }
    bool PreCodeReadHook(bool, uint32_t, A32::IREmitter &) override { return true; }
    void PreCodeTranslationHook(bool, uint32_t, A32::IREmitter &) override {}
    uint64_t GetTicksForCode(bool, uint32_t, uint32_t) override { return 1; }

    Value get(const IR::Value &v) {
        return v.IsImmediate() ? Value(v.GetImmediateAsU64()) : values.at(v.GetInst());
    }
    void location(const IR::LocationDescriptor &next) {
        const A32::LocationDescriptor loc(next);
        s.regs[15] = loc.PC();
        s.cpsr = (s.cpsr & ~A32::LocationDescriptor::CPSR_MODE_MASK) | loc.CPSR().Value();
    }
    struct Terminal : boost::static_visitor<> {
        Impl &self;
        bool execute;
        Terminal(Impl &self_, bool execute_)
            : self(self_)
            , execute(execute_) {}
        void operator()(const IR::Term::Invalid &) const { throw std::runtime_error("Invalid IR terminal"); }
        void operator()(const IR::Term::Interpret &) const { throw std::runtime_error("Instruction requires an unsupported ARM fallback"); }
        void operator()(const IR::Term::ReturnToDispatch &) const {}
        void operator()(const IR::Term::PopRSBHint &) const {}
        void operator()(const IR::Term::FastDispatchHint &) const {}
        void operator()(const IR::Term::LinkBlock &t) const {
            if (execute)
                self.location(t.next);
        }
        void operator()(const IR::Term::LinkBlockFast &t) const {
            if (execute)
                self.location(t.next);
        }
        void operator()(const IR::Term::CheckHalt &t) const { boost::apply_visitor(*this, t.else_); }
        void operator()(const IR::Term::If &t) const {
            if (!execute) {
                boost::apply_visitor(*this, t.then_);
                boost::apply_visitor(*this, t.else_);
            } else
                boost::apply_visitor(*this, condition(t.if_, self.s.cpsr) ? t.then_ : t.else_);
        }
        void operator()(const IR::Term::CheckBit &t) const {
            if (!execute) {
                boost::apply_visitor(*this, t.then_);
                boost::apply_visitor(*this, t.else_);
            } else
                boost::apply_visitor(*this, self.check_bit ? t.then_ : t.else_);
        }
    };

    Value operation(const IR::Inst &inst, bool execute) {
        const auto arg = [&](size_t n) { return get(inst.GetArg(n)); };
        const auto u32 = [&](size_t n) { return static_cast<uint32_t>(arg(n).bits); };
        const auto set_flags = [&](uint32_t flags, uint32_t mask) { s.cpsr = (s.cpsr & ~mask) | (flags & mask); };
        // Validation visits the identical opcode dispatch before any side effect.
        switch (inst.GetOpcode()) {
        case Op::Void:
            if (!execute)
                return {};
            return {};
        case Op::PushRSB:
            if (!execute)
                return {};
            return {};
        case Op::A32ExceptionRaised: {
            const auto exception = static_cast<A32::Exception>(inst.GetArg(1).GetU64());
            switch (exception) {
            case A32::Exception::WaitForInterrupt:
                if (execute)
                    s.halted = true;
                return {};
            case A32::Exception::Breakpoint:
                if (execute) {
                    s.breakpoint = true;
                    s.regs[15] = u32(0);
                }
                return {};
            case A32::Exception::PreloadDataWithIntentToWrite:
            case A32::Exception::PreloadData:
            case A32::Exception::PreloadInstruction:
            case A32::Exception::SendEvent:
            case A32::Exception::SendEventLocal:
            case A32::Exception::WaitForEvent:
            case A32::Exception::Yield:
                return {};
            default: throw std::runtime_error("Unsupported ARM exception: " + std::to_string(static_cast<int>(exception)));
            }
        }
        case Op::Identity:
            if (!execute)
                return {};
            return arg(0);
        case Op::A32GetRegister:
            if (!execute)
                return {};
            return s.regs.at(static_cast<size_t>(inst.GetArg(0).GetA32RegRef()));
        case Op::A32SetRegister:
            if (!execute)
                return {};
            s.regs.at(static_cast<size_t>(inst.GetArg(0).GetA32RegRef()))
                = u32(1);
            return {};
        case Op::A32GetCpsr:
            if (!execute)
                return {};
            return s.cpsr;
        case Op::A32SetCpsr:
            if (!execute)
                return {};
            s.cpsr
                = u32(0);
            return {};
        case Op::A32GetCFlag:
            if (!execute)
                return {};
            return (s.cpsr >> 29) & 1;
        case Op::A32SetCpsrNZCV:
            if (!execute)
                return {};
            set_flags(u32(0), 0xf0000000);
            return {};
        case Op::A32SetCpsrNZCVRaw:
            if (!execute)
                return {};
            set_flags(u32(0), 0xf0000000);
            return {};
        case Op::A32SetCpsrNZCVQ:
            if (!execute)
                return {};
            set_flags(u32(0), 0xf8000000);
            return {};
        case Op::A32SetCpsrNZ:
            if (!execute)
                return {};
            set_flags(u32(0), 0xc0000000);
            return {};
        case Op::A32SetCpsrNZC:
            if (!execute)
                return {};
            set_flags((u32(0) & 0xc0000000) | (u32(1) << 29), 0xe0000000);
            return {};
        case Op::A32OrQFlag:
            if (!execute)
                return {};
            s.cpsr
                |= u32(0) << 27;
            return {};
        case Op::A32SetCheckBit:
            if (!execute)
                return {};
            check_bit = u32(0);
            return {};
        case Op::A32BXWritePC:
            if (!execute)
                return {};
            {
                const uint32_t pc = u32(0);
                s.cpsr = (s.cpsr & ~0x20u) | ((pc & 1) << 5);
                s.regs[15] = pc & (pc & 1 ? ~1u : ~3u);
                return {};
            }
        case Op::A32UpdateUpperLocationDescriptor:
            if (!execute)
                return {};
            set_flags(end.CPSR().Value(), A32::LocationDescriptor::CPSR_MODE_MASK);
            return {};
        case Op::A32CallSupervisor:
            if (!execute)
                return {};
            s.svc_called
                = true;
            s.svc = u32(0);
            return {};
        case Op::A32DataSynchronizationBarrier:
            if (!execute)
                return {};
            std::atomic_thread_fence(std::memory_order_seq_cst);
            return {};
        case Op::A32DataMemoryBarrier:
            if (!execute)
                return {};
            std::atomic_thread_fence(std::memory_order_seq_cst);
            return {};
        case Op::A32InstructionSynchronizationBarrier:
            if (!execute)
                return {};
            return {};
        case Op::GetCarryFromOp:
            if (!execute)
                return {};
            return arg(0).carry;
        case Op::GetOverflowFromOp:
            if (!execute)
                return {};
            return arg(0).overflow;
        case Op::GetNZCVFromOp:
            if (!execute)
                return {};
            return arg(0).flags;
        case Op::GetNZFromOp:
            if (!execute)
                return {};
            {
                const auto value = arg(0).bits;
                const bool wide = inst.GetArg(0).GetType() == IR::Type::U64;
                return (static_cast<uint32_t>((value >> (wide ? 63 : 31)) & 1) << 31)
                    | (value == 0 ? 0x40000000u : 0u);
            }
        case Op::GetCFlagFromNZCV:
            if (!execute)
                return {};
            return (u32(0) >> 29) & 1;
        case Op::NZCVFromPackedFlags:
            if (!execute)
                return {};
            return u32(0) & 0xf0000000;
        case Op::Pack2x32To1x64:
            if (!execute)
                return {};
            return uint64_t(u32(0)) | (uint64_t(u32(1)) << 32);
        case Op::LeastSignificantWord:
            if (!execute)
                return {};
            return u32(0);
        case Op::LeastSignificantHalf:
            if (!execute)
                return {};
            return u32(0) & 0xffff;
        case Op::LeastSignificantByte:
            if (!execute)
                return {};
            return u32(0) & 0xff;
        case Op::MostSignificantWord:
            if (!execute)
                return {};
            return arg(0).bits
                >> 32;
        case Op::MostSignificantBit:
            if (!execute)
                return {};
            return u32(0) >> 31;
        case Op::IsZero32:
            if (!execute)
                return {};
            return u32(0) == 0;
        case Op::IsZero64:
            if (!execute)
                return {};
            return arg(0).bits
                == 0;
        case Op::Add32:
        case Op::Sub32: {
            if (!execute)
                return {};
            const uint32_t a = u32(0), b = inst.GetOpcode() == Op::Add32 ? u32(1) : ~u32(1);
            const uint64_t sum = uint64_t(a) + b + u32(2);
            Value result(static_cast<uint32_t>(sum));
            result.carry = sum >> 32;
            result.overflow = ((~(a ^ b) & (a ^ result.bits)) >> 31) & 1;
            result.flags |= (uint32_t(result.carry) << 29) | (uint32_t(result.overflow) << 28);
            return result;
        }
        case Op::Mul32:
            if (!execute)
                return {};
            return u32(0) * u32(1);
        case Op::Mul64:
            if (!execute)
                return {};
            return arg(0).bits
                * arg(1).bits;
        case Op::And32:
            if (!execute)
                return {};
            return u32(0) & u32(1);
        case Op::Eor32:
            if (!execute)
                return {};
            return u32(0) ^ u32(1);
        case Op::Or32:
            if (!execute)
                return {};
            return u32(0) | u32(1);
        case Op::Not32:
            if (!execute)
                return {};
            return ~u32(0);
        case Op::And64:
            if (!execute)
                return {};
            return arg(0).bits
                & arg(1).bits;
        case Op::Eor64:
            if (!execute)
                return {};
            return arg(0).bits
                ^ arg(1).bits;
        case Op::Or64:
            if (!execute)
                return {};
            return arg(0).bits
                | arg(1).bits;
        case Op::Not64:
            if (!execute)
                return {};
            return ~arg(0).bits;
        case Op::ZeroExtendByteToWord:
            if (!execute)
                return {};
            return u32(0) & 0xff;
        case Op::ZeroExtendHalfToWord:
            if (!execute)
                return {};
            return u32(0) & 0xffff;
        case Op::ZeroExtendWordToLong:
            if (!execute)
                return {};
            return u32(0);
        case Op::SignExtendByteToWord:
            if (!execute)
                return {};
            return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(u32(0))));
        case Op::SignExtendHalfToWord:
            if (!execute)
                return {};
            return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(u32(0))));
        case Op::SignExtendWordToLong:
            if (!execute)
                return {};
            return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(u32(0))));
        case Op::CountLeadingZeros32:
            if (!execute)
                return {};
            return std::countl_zero(u32(0));
        case Op::RotateRightExtended:
            if (!execute)
                return {};
            {
                Value result((u32(0) >> 1) | (u32(1) << 31));
                result.carry = u32(0) & 1;
                return result;
            }
        case Op::LogicalShiftLeft32:
        case Op::LogicalShiftRight32:
        case Op::ArithmeticShiftRight32:
        case Op::RotateRight32: {
            if (!execute)
                return {};
            const uint32_t value = u32(0), amount = u32(1);
            Value result(value);
            result.carry = u32(2);
            if (!amount)
                return result;
            switch (inst.GetOpcode()) {
            case Op::LogicalShiftLeft32:
                result = Value(amount < 32 ? value << amount : 0);
                result.carry = amount <= 32 && ((value >> (32 - amount)) & 1);
                break;
            case Op::LogicalShiftRight32:
                result = Value(amount < 32 ? value >> amount : 0);
                result.carry = amount <= 32 && ((value >> (amount - 1)) & 1);
                break;
            case Op::ArithmeticShiftRight32:
                result = Value(static_cast<uint32_t>(static_cast<int32_t>(value) >> std::min(amount, 31u)));
                result.carry = (value >> (std::min(amount, 32u) - 1)) & 1;
                break;
            default:
                result = Value(std::rotr(value, static_cast<int>(amount & 31)));
                result.carry = result.bits >> 31;
                break;
            }
            return result;
        }
        case Op::A32ReadMemory8:
            if (!execute)
                return {};
            return read(u32(1), 1);
        case Op::A32ReadMemory16:
            if (!execute)
                return {};
            return read(u32(1), 2);
        case Op::A32ReadMemory32:
            if (!execute)
                return {};
            return read(u32(1), 4);
        case Op::A32ReadMemory64:
            if (!execute)
                return {};
            return read(u32(1), 8);
        case Op::A32WriteMemory8:
            if (!execute)
                return {};
            write(u32(1), arg(2).bits, 1);
            return {};
        case Op::A32WriteMemory16:
            if (!execute)
                return {};
            write(u32(1), arg(2).bits, 2);
            return {};
        case Op::A32WriteMemory32:
            if (!execute)
                return {};
            write(u32(1), arg(2).bits, 4);
            return {};
        case Op::A32WriteMemory64:
            if (!execute)
                return {};
            write(u32(1), arg(2).bits, 8);
            return {};
        default: throw std::runtime_error("Unsupported IR opcode: " + std::string(IR::GetNameOf(inst.GetOpcode())));
        }
    }

    bool step() {
        failure.clear();
        s.svc_called = false;
        s.halted = false;
        s.breakpoint = false;
        values.clear();
        try {
            if (s.cpsr & 0x200)
                throw std::runtime_error("Big-endian interpreter execution is unsupported");
            A32::LocationDescriptor location(s.regs[15], A32::PSR{ s.cpsr }, A32::FPSCR{ s.fpscr }, true);
            auto block = A32::Translate(location, this, { A32::ArchVersion::v7, false, true });
            end = A32::LocationDescriptor(block.EndLocation());
            if (!condition(block.GetCondition(), s.cpsr)) {
                this->location(block.ConditionFailedLocation());
                return true;
            }
            if (block.size() > 256)
                throw std::runtime_error("Interpreter instruction exceeds 256 IR operations");
            for (const auto &inst : block)
                operation(inst, false);
            boost::apply_visitor(Terminal(*this, false), block.GetTerminal());
            values.reserve(block.size());
            for (const auto &inst : block)
                values.emplace(&inst, operation(inst, true));
            boost::apply_visitor(Terminal(*this, true), block.GetTerminal());
            return true;
        } catch (const std::exception &e) {
            failure = e.what();
            return false;
        }
    }
};
IRInterpreter::IRInterpreter(Read read, Write write)
    : impl(std::make_unique<Impl>(state, std::move(read), std::move(write))) {}
IRInterpreter::~IRInterpreter() = default;
bool IRInterpreter::step() { return impl->step(); }
const std::string &IRInterpreter::error() const { return impl->failure; }
} // namespace cpu
