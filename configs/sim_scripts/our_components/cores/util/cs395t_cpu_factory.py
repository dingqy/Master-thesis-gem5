import importlib
import platform
from gem5.utils.requires import requires
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_core import SimpleCore 
from gem5.isas import ISA
from ..cs395t_core import CS395T_CPU

class CS395T_SimpleCore(SimpleCore):
    @classmethod
    def cs395t_cpu_factory(cls, cpu_type: CPUTypes, isa: ISA, core_id: int):
        # CS395T: Return the CS395T CPU instead!
        if cpu_type == CPUTypes.O3:
            return CS395T_CPU(cpu_id=core_id)

        # Otherwise, default behavior...
        """
        A factory used to return the SimObject core object given the cpu type,
        and ISA target. An exception will be thrown if there is an
        incompatibility.

        :param cpu_type: The target CPU type.
        :param isa: The target ISA.
        :param core_id: The id of the core to be returned.
        """
        requires(isa_required=isa)

        _isa_string_map = {
            ISA.X86 : "X86",
            ISA.ARM : "Arm",
            ISA.RISCV : "Riscv",
            ISA.SPARC : "Sparc",
            ISA.POWER : "Power",
            ISA.MIPS : "Mips",
        }

        _cpu_types_string_map = {
            CPUTypes.ATOMIC : "AtomicSimpleCPU",
            CPUTypes.O3 : "O3CPU",
            CPUTypes.TIMING : "TimingSimpleCPU",
            CPUTypes.KVM : "KvmCPU",
            CPUTypes.MINOR : "MinorCPU",
        }

        if isa not in _isa_string_map:
            raise NotImplementedError(f"ISA '{isa.name}' does not have an"
                "entry in `AbstractCore.cpu_simobject_factory._isa_string_map`"
            )

        if cpu_type not in _cpu_types_string_map:
            raise NotImplementedError(f"CPUType '{cpu_type.name}' "
                "does not have an entry in "
                "`AbstractCore.cpu_simobject_factory._cpu_types_string_map`"
            )

        if cpu_type == CPUTypes.KVM:
            # For some reason, the KVM CPU is under "m5.objects" not the
            # "m5.objects.{ISA}CPU".
            module_str = f"m5.objects"
        else:
            module_str = f"m5.objects.{_isa_string_map[isa]}CPU"

        # GEM5 compiles two versions of KVM for ARM depending upon the host CPU
        # : ArmKvmCPU and ArmV8KvmCPU for 32 bit (Armv7l) and 64 bit (Armv8)
        # respectively.

        if isa.name == "ARM" and \
                cpu_type == CPUTypes.KVM and \
                platform.architecture()[0] == "64bit":
            cpu_class_str = f"{_isa_string_map[isa]}V8"\
                            f"{_cpu_types_string_map[cpu_type]}"
        else:
            cpu_class_str = f"{_isa_string_map[isa]}"\
                            f"{_cpu_types_string_map[cpu_type]}"

        try:
            to_return_cls = getattr(importlib.import_module(module_str),
                                    cpu_class_str
                                   )
        except ImportError:
            raise Exception(
                f"Cannot find CPU type '{cpu_type.name}' for '{isa.name}' "
                "ISA. Please ensure you have compiled the correct version of "
                "gem5."
            )

        return to_return_cls(cpu_id=core_id)
