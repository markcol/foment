/*

Foment

*/

#include <windows.h>
//#include <stdio.h>
#include <malloc.h>
#include "foment.hpp"
#include "execute.hpp"
#include "syncthrd.hpp"

static FObject DynamicEnvironment;

// ---- Procedure ----

static FObject MakeProcedure(FObject nam, FObject cv, int ac, FObject ra, unsigned int fl)
{
    FProcedure * p = (FProcedure *) MakeObject(sizeof(FProcedure), ProcedureTag);
    p->Reserved = MakeLength(ac, ProcedureTag) | fl;
    p->Name = SyntaxToDatum(nam);
    p->Code = cv;
    p->RestArg = ra;

    return(p);
}

FObject MakeProcedure(FObject nam, FObject cv, int ac, FObject ra)
{
    return(MakeProcedure(nam, cv, ac, ra, 0));
}

// ---- Instruction ----

static char * Opcodes[] =
{
    "check-count",
    "rest-arg",
    "make-list",
    "push-cstack",
    "push-no-value",
    "push-want-values",
    "pop-cstack",
    "save-frame",
    "restore-frame",
    "make-frame",
    "push-frame",
    "get-cstack",
    "set-cstack",
    "get-frame",
    "set-frame",
    "get-vector",
    "set-vector",
    "get-global",
    "set-global",
    "get-box",
    "set-box",
    "discard-result",
    "pop-astack",
    "duplicate",
    "return",
    "call",
    "call-proc",
    "call-prim",
    "tail-call",
    "tail-call-proc",
    "tail-call-prim",
    "set-arg-count",
    "make-closure",
    "if-false",
    "if-eqv?",
    "goto-relative",
    "goto-absolute",
    "check-values",
    "rest-values",
    "values",
    "apply",
    "case-lambda"
};

void WriteInstruction(FObject port, FObject obj, int df)
{
    FAssert(InstructionP(obj));

    FCh s[16];
    int sl = NumberAsString(InstructionArg(obj), s, 10);

    PutStringC(port, "#<");

    if (InstructionOpcode(obj) < 0 || InstructionOpcode(obj) >= sizeof(Opcodes) / sizeof(char *))
        PutStringC(port, "unknown");
    else
        PutStringC(port, Opcodes[InstructionOpcode(obj)]);

    PutStringC(port, ": ");
    PutString(port, s, sl);
    PutCh(port, '>');
}

// --------

FObject Execute(FObject op, int argc, FObject argv[])
{
    FThreadState * ts = GetThreadState();

    ts->AStackPtr = 0;

    int adx;
    for (adx = 0; adx < argc; adx++)
    {
        ts->AStack[ts->AStackPtr] = argv[adx];
        ts->AStackPtr += 1;
    }

    ts->CStackPtr = 0;
    ts->Proc = op;
    FAssert(ProcedureP(ts->Proc));
    FAssert(VectorP(AsProcedure(ts->Proc)->Code));

    ts->IP = 0;
    ts->Frame = NoValueObject;
    ts->ArgCount = argc;

    try
    {
        for (;;)
        {
            CheckForGC();

            FAssert(VectorP(AsProcedure(ts->Proc)->Code));
            FAssert(ts->IP >= 0);
            FAssert(ts->IP < (int) VectorLength(AsProcedure(ts->Proc)->Code));

            FObject obj = AsVector(AsProcedure(ts->Proc)->Code)->Vector[ts->IP];
            ts->IP += 1;

            if (InstructionP(obj) == 0)
            {
                ts->AStack[ts->AStackPtr] = obj;
                ts->AStackPtr += 1;
//Write(StandardOutput, obj, 0);
//printf("\n");
            }
            else
            {
//printf("%s.%d %d %d\n", Opcodes[InstructionOpcode(obj)], InstructionArg(obj), ts->CStackPtr, ts->AStackPtr);
                switch (InstructionOpcode(obj))
                {
                case CheckCountOpcode:
                    if (ts->ArgCount != InstructionArg(obj))
                        RaiseException(R.Assertion, AsProcedure(ts->Proc)->Name,
                                R.WrongNumberOfArguments, EmptyListObject);
                    break;

                case RestArgOpcode:
                    FAssert(InstructionArg(obj) >= 0);

                    if (ts->ArgCount < InstructionArg(obj))
                        RaiseException(R.Assertion, AsProcedure(ts->Proc)->Name,
                                R.WrongNumberOfArguments, EmptyListObject);
                    else if (ts->ArgCount == InstructionArg(obj))
                    {
                        ts->AStack[ts->AStackPtr] = EmptyListObject;
                        ts->AStackPtr += 1;
                    }
                    else
                    {
                        FObject lst = EmptyListObject;

                        int ac = ts->ArgCount;
                        while (ac > InstructionArg(obj))
                        {
                            ts->AStackPtr -= 1;
                            lst = MakePair(ts->AStack[ts->AStackPtr], lst);
                            ac -= 1;
                        }

                        ts->AStack[ts->AStackPtr] = lst;
                        ts->AStackPtr += 1;
                    }
                    break;

                case MakeListOpcode:
                {
                    FAssert(InstructionArg(obj) > 0);

                    FObject lst = EmptyListObject;
                    int ac = InstructionArg(obj);
                    while (ac > 0)
                    {
                        ts->AStackPtr -= 1;
                        lst = MakePair(ts->AStack[ts->AStackPtr], lst);
                        ac -= 1;
                    }

                    ts->AStack[ts->AStackPtr] = lst;
                    ts->AStackPtr += 1;
                    break;
                }

                case PushCStackOpcode:
                {
                    FFixnum arg = InstructionArg(obj);

                    while (arg > 0)
                    {
                        FAssert(ts->AStackPtr > 0);

                        ts->AStackPtr -= 1;
                        ts->CStack[- ts->CStackPtr] = ts->AStack[ts->AStackPtr];
                        ts->CStackPtr += 1;
                        arg -= 1;
                    }
                    break;
                }

                case PushNoValueOpcode:
                {
                    FFixnum arg = InstructionArg(obj);

                    while (arg > 0)
                    {
                        ts->CStack[- ts->CStackPtr] = NoValueObject;
                        ts->CStackPtr += 1;
                        arg -= 1;
                    }

                    break;
                }

                case PushWantValuesOpcode:
                    ts->CStack[- ts->CStackPtr] = WantValuesObject;
                    ts->CStackPtr += 1;
                    break;

                case PopCStackOpcode:
                    FAssert(ts->CStackPtr >= InstructionArg(obj));

                    ts->CStackPtr -= InstructionArg(obj);
                    break;

                case SaveFrameOpcode:
                    ts->CStack[- ts->CStackPtr] = ts->Frame;
                    ts->CStackPtr += 1;
                    break;

                case RestoreFrameOpcode:
                    FAssert(ts->CStackPtr > 0);

                    ts->CStackPtr -= 1;
                    ts->Frame = ts->CStack[- ts->CStackPtr];
                    break;

                case MakeFrameOpcode:
                    ts->Frame = MakeVector(InstructionArg(obj), 0, NoValueObject);
                    break;

                case PushFrameOpcode:
                    ts->AStack[ts->AStackPtr] = ts->Frame;
                    ts->AStackPtr += 1;
                    break;

                case GetCStackOpcode:
                    FAssert(InstructionArg(obj) <= ts->CStackPtr);
                    FAssert(InstructionArg(obj) > 0);

                    ts->AStack[ts->AStackPtr] = ts->CStack[- (ts->CStackPtr - InstructionArg(obj))];
                    ts->AStackPtr += 1;
                    break;

                case SetCStackOpcode:
                    FAssert(InstructionArg(obj) <= ts->CStackPtr);
                    FAssert(InstructionArg(obj) > 0);
                    FAssert(ts->AStackPtr > 0);

                    ts->AStackPtr -= 1;
                    ts->CStack[- (ts->CStackPtr - InstructionArg(obj))] = ts->AStack[ts->AStackPtr];
                    break;

                case GetFrameOpcode:
                    FAssert(VectorP(ts->Frame));
                    FAssert(InstructionArg(obj) < VectorLength(ts->Frame));

                    ts->AStack[ts->AStackPtr] = AsVector(ts->Frame)->Vector[InstructionArg(obj)];
                    ts->AStackPtr += 1;
                    break;

                case SetFrameOpcode:
                    FAssert(VectorP(ts->Frame));
                    FAssert(InstructionArg(obj) < VectorLength(ts->Frame));
                    FAssert(ts->AStackPtr > 0);

                    ts->AStackPtr -= 1;
    //                AsVector(ts->Frame)->Vector[InstructionArg(obj)] = ts->AStack[ts->AStackPtr];
                    ModifyVector(ts->Frame, InstructionArg(obj), ts->AStack[ts->AStackPtr]);
                    break;

                case GetVectorOpcode:
                    FAssert(ts->AStackPtr > 0);
                    FAssert(VectorP(ts->AStack[ts->AStackPtr - 1]));
                    FAssert(InstructionArg(obj) < VectorLength(ts->AStack[ts->AStackPtr - 1]));

                    ts->AStack[ts->AStackPtr - 1] = AsVector(ts->AStack[ts->AStackPtr - 1])->Vector[
                            InstructionArg(obj)];
                    break;

                case SetVectorOpcode:
                    FAssert(ts->AStackPtr > 1);
                    FAssert(VectorP(ts->AStack[ts->AStackPtr - 1]));
                    FAssert(InstructionArg(obj) < VectorLength(ts->AStack[ts->AStackPtr - 1]));

//                    AsVector(ts->AStack[ts->AStackPtr - 1])->Vector[InstructionArg(obj)] =
//                            ts->AStack[ts->AStackPtr - 2];
                    ModifyVector(ts->AStack[ts->AStackPtr - 1], InstructionArg(obj),
                            ts->AStack[ts->AStackPtr - 2]);
                    ts->AStackPtr -= 2;
                    break;

                case GetGlobalOpcode:
                    FAssert(ts->AStackPtr > 0);
                    FAssert(GlobalP(ts->AStack[ts->AStackPtr - 1]));
                    FAssert(BoxP(AsGlobal(ts->AStack[ts->AStackPtr - 1])->Box));

                    if (AsGlobal(ts->AStack[ts->AStackPtr - 1])->State == GlobalUndefined)
                    {
                        FAssert(AsGlobal(ts->AStack[ts->AStackPtr - 1])->Interactive == TrueObject);

                        RaiseException(R.Assertion, AsProcedure(ts->Proc)->Name, R.UndefinedMessage,
                                List(ts->AStack[ts->AStackPtr - 1]));
                    }

                    ts->AStack[ts->AStackPtr - 1] = Unbox(
                            AsGlobal(ts->AStack[ts->AStackPtr - 1])->Box);
                    break;

                case SetGlobalOpcode:
                    FAssert(ts->AStackPtr > 1);
                    FAssert(GlobalP(ts->AStack[ts->AStackPtr - 1]));
                    FAssert(BoxP(AsGlobal(ts->AStack[ts->AStackPtr - 1])->Box));

                    if (AsGlobal(ts->AStack[ts->AStackPtr - 1])->State == GlobalImported
                            || AsGlobal(ts->AStack[ts->AStackPtr - 1])->State == GlobalImportedModified)
                    {
                        FAssert(AsGlobal(ts->AStack[ts->AStackPtr - 1])->Interactive == TrueObject);

//                        AsGlobal(ts->AStack[ts->AStackPtr - 1])->Box = MakeBox(
//                                ts->AStack[ts->AStackPtr - 2]);
                        Modify(FGlobal, ts->AStack[ts->AStackPtr - 1], Box,
                                MakeBox(ts->AStack[ts->AStackPtr - 2]));
//                        AsGlobal(ts->AStack[ts->AStackPtr - 1])->State = GlobalDefined;
                        Modify(FGlobal, ts->AStack[ts->AStackPtr - 1], State, GlobalDefined);
                    }

//                    AsBox(AsGlobal(ts->AStack[ts->AStackPtr - 1])->Box)->Value =
                            ts->AStack[ts->AStackPtr - 2];
                    Modify(FBox, AsGlobal(ts->AStack[ts->AStackPtr - 1])->Box, Value,
                            ts->AStack[ts->AStackPtr - 2]);
                    ts->AStackPtr -= 2;
                    break;

                case GetBoxOpcode:
                    FAssert(ts->AStackPtr > 0);
                    FAssert(BoxP(ts->AStack[ts->AStackPtr - 1]));

                    ts->AStack[ts->AStackPtr - 1] = Unbox(ts->AStack[ts->AStackPtr - 1]);
                    break;

                case SetBoxOpcode:
                    FAssert(ts->AStackPtr > 1);
                    FAssert(BoxP(ts->AStack[ts->AStackPtr - 1]));

//                    AsBox(ts->AStack[ts->AStackPtr - 1])->Value = ts->AStack[ts->AStackPtr - 2];
                    Modify(FBox, ts->AStack[ts->AStackPtr - 1], Value, ts->AStack[ts->AStackPtr - 2]);
                    ts->AStackPtr -= 2;
                    break;

                case DiscardResultOpcode:
                    FAssert(ts->AStackPtr >= 1);

                    ts->AStackPtr -= 1;
                    break;

                case PopAStackOpcode:
                    FAssert(ts->AStackPtr >= InstructionArg(obj));

                    ts->AStackPtr -= InstructionArg(obj);
                    break;

                case DuplicateOpcode:
                    FAssert(ts->AStackPtr >= 1);

                    ts->AStack[ts->AStackPtr] = ts->AStack[ts->AStackPtr - 1];
                    ts->AStackPtr += 1;
                    break;

                case ReturnOpcode:
                    if (ts->CStackPtr == 0)
                    {
                        FAssert(ts->AStackPtr == 1);

                        ts->AStackPtr -= 1;
                        FObject ret = ts->AStack[0];

                        ts->Proc = NoValueObject;
                        ts->Frame = NoValueObject;

                        return(ret);
                    }

                    FAssert(ts->CStackPtr >= 2);
                    FAssert(FixnumP(ts->CStack[- (ts->CStackPtr - 1)]));
                    FAssert(ProcedureP(ts->CStack[- (ts->CStackPtr - 2)]));

                    ts->CStackPtr -= 1;
                    ts->IP = AsFixnum(ts->CStack[- ts->CStackPtr]);
                    ts->CStackPtr -= 1;
                    ts->Proc = ts->CStack[- ts->CStackPtr];

                    FAssert(VectorP(AsProcedure(ts->Proc)->Code));

                    ts->Frame = NoValueObject;
                    break;

                case CallOpcode:
                    FAssert(ts->AStackPtr > 0);

                    ts->AStackPtr -= 1;
                    op = ts->AStack[ts->AStackPtr];
                    if (ProcedureP(op))
                    {
CallProcedure:
                        ts->CStack[- ts->CStackPtr] = ts->Proc;
                        ts->CStackPtr += 1;
                        ts->CStack[- ts->CStackPtr] = MakeFixnum(ts->IP);
                        ts->CStackPtr += 1;

                        ts->Proc = op;
                        FAssert(VectorP(AsProcedure(ts->Proc)->Code));

                        ts->IP = 0;
                        ts->Frame = NoValueObject;
                    }
                    else if (PrimitiveP(op))
                    {
CallPrimitive:
                        FAssert(ts->AStackPtr >= ts->ArgCount);

                        FObject ret = AsPrimitive(op)->PrimitiveFn(ts->ArgCount,
                                ts->AStack + ts->AStackPtr - ts->ArgCount);
                        ts->AStackPtr -= ts->ArgCount;
                        ts->AStack[ts->AStackPtr] = ret;
                        ts->AStackPtr += 1;
                    }
                    else
                        RaiseException(R.Assertion, AsProcedure(ts->Proc)->Name, R.NotCallable,
                                List(op));
                    break;

                case CallProcOpcode:
                    FAssert(ts->AStackPtr > 0);

                    ts->AStackPtr -= 1;
                    op = ts->AStack[ts->AStackPtr];

                    FAssert(ProcedureP(op));
                    goto CallProcedure;

                case CallPrimOpcode:
                    FAssert(ts->AStackPtr > 0);

                    ts->AStackPtr -= 1;
                    op = ts->AStack[ts->AStackPtr];

                    FAssert(PrimitiveP(op));
                    goto CallPrimitive;

                case TailCallOpcode:
                    FAssert(ts->AStackPtr > 0);

                    ts->AStackPtr -= 1;
                    op = ts->AStack[ts->AStackPtr];
                    if (ProcedureP(op))
                    {
TailCallProcedure:
                        ts->Proc = op;
                        FAssert(VectorP(AsProcedure(ts->Proc)->Code));

                        ts->IP = 0;
                        ts->Frame = NoValueObject;
                    }
                    else if (PrimitiveP(op))
                    {
TailCallPrimitive:
                        FAssert(ts->AStackPtr >= ts->ArgCount);

                        FObject ret = AsPrimitive(op)->PrimitiveFn(ts->ArgCount,
                                ts->AStack + ts->AStackPtr - ts->ArgCount);
                        ts->AStackPtr -= ts->ArgCount;
                        ts->AStack[ts->AStackPtr] = ret;
                        ts->AStackPtr += 1;

                        if (ts->CStackPtr == 0)
                        {
                            FAssert(ts->AStackPtr == 1);

                            ts->AStackPtr -= 1;
                            FObject ret = ts->AStack[0];

                            ts->Proc = NoValueObject;
                            ts->Frame = NoValueObject;

                            return(ret);
                        }

                        FAssert(ts->CStackPtr >= 2);

                        ts->CStackPtr -= 1;
                        ts->IP = AsFixnum(ts->CStack[- ts->CStackPtr]);
                        ts->CStackPtr -= 1;
                        ts->Proc = ts->CStack[- ts->CStackPtr];

                        FAssert(ProcedureP(ts->Proc));
                        FAssert(VectorP(AsProcedure(ts->Proc)->Code));

                        ts->Frame = NoValueObject;
                    }
                    else
                        RaiseException(R.Assertion, AsProcedure(ts->Proc)->Name, R.NotCallable,
                                List(op));
                    break;

                case TailCallProcOpcode:
                    FAssert(ts->AStackPtr > 0);

                    ts->AStackPtr -= 1;
                    op = ts->AStack[ts->AStackPtr];

                    FAssert(ProcedureP(op));

                    goto TailCallProcedure;

                case TailCallPrimOpcode:
                    FAssert(ts->AStackPtr > 0);

                    ts->AStackPtr -= 1;
                    op = ts->AStack[ts->AStackPtr];

                    FAssert(PrimitiveP(op));

                    goto TailCallPrimitive;

                case SetArgCountOpcode:
                    ts->ArgCount = InstructionArg(obj);
                    break;

                case MakeClosureOpcode:
                {
                    FAssert(ts->AStackPtr > 0);

                    FObject v[3];

                    ts->AStackPtr -= 1;
                    v[0] = ts->AStack[ts->AStackPtr - 1];
                    v[1] = ts->AStack[ts->AStackPtr];
                    v[2] = MakeInstruction(TailCallOpcode, 0);
                    FObject proc = MakeProcedure(NoValueObject, MakeVector(3, v, NoValueObject), 0,
                            FalseObject, PROCEDURE_FLAG_CLOSURE);
                    ts->AStack[ts->AStackPtr - 1] = proc;
                    break;
                }

                case IfFalseOpcode:
                    FAssert(ts->AStackPtr > 0);
                    FAssert(ts->IP + InstructionArg(obj) >= 0);

                    ts->AStackPtr -= 1;
                    if (ts->AStack[ts->AStackPtr] == FalseObject)
                        ts->IP += InstructionArg(obj);
                    break;

                case IfEqvPOpcode:
                    FAssert(ts->AStackPtr > 1);
                    FAssert(ts->IP + InstructionArg(obj) >= 0);

                    ts->AStackPtr -= 1;
                    if (ts->AStack[ts->AStackPtr] == ts->AStack[ts->AStackPtr - 1])
                        ts->IP += InstructionArg(obj);
                    break;

                case GotoRelativeOpcode:
                    FAssert(ts->IP + InstructionArg(obj) >= 0);

                    ts->IP += InstructionArg(obj);
                    break;

                case GotoAbsoluteOpcode:
                    FAssert(InstructionArg(obj) >= 0);

                    ts->IP = InstructionArg(obj);
                    break;

                case CheckValuesOpcode:
                    FAssert(ts->AStackPtr > 0);
                    FAssert(InstructionArg(obj) != 1);

                    if (ValuesCountP(ts->AStack[ts->AStackPtr - 1]))
                    {
                        ts->AStackPtr -= 1;
                        if (AsValuesCount(ts->AStack[ts->AStackPtr]) != InstructionArg(obj))
                            RaiseException(R.Assertion, AsProcedure(ts->Proc)->Name,
                                    R.UnexpectedNumberOfValues, EmptyListObject);
                    }
                    else
                        RaiseException(R.Assertion, AsProcedure(ts->Proc)->Name,
                                R.UnexpectedNumberOfValues, EmptyListObject);
                    break;

                case RestValuesOpcode:
                {
                    FAssert(ts->AStackPtr > 0);
                    FAssert(InstructionArg(obj) >= 0);

                    int vc;

                    if (ValuesCountP(ts->AStack[ts->AStackPtr - 1]))
                    {
                        ts->AStackPtr -= 1;
                        vc = AsValuesCount(ts->AStack[ts->AStackPtr]);
                    }
                    else
                        vc = 1;

                    if (vc < InstructionArg(obj))
                        RaiseException(R.Assertion, AsProcedure(ts->Proc)->Name,
                                R.UnexpectedNumberOfValues, EmptyListObject);
                    else if (vc == InstructionArg(obj))
                    {
                        ts->AStack[ts->AStackPtr] = EmptyListObject;
                        ts->AStackPtr += 1;
                    }
                    else
                    {
                        FObject lst = EmptyListObject;

                        while (vc > InstructionArg(obj))
                        {
                            ts->AStackPtr -= 1;
                            lst = MakePair(ts->AStack[ts->AStackPtr], lst);
                            vc -= 1;
                        }

                        ts->AStack[ts->AStackPtr] = lst;
                        ts->AStackPtr += 1;
                    }

                    break;
                }

                case ValuesOpcode:
                    if (ts->ArgCount != 1)
                    {
                        if (ts->CStackPtr >= 3
                                && WantValuesObjectP(ts->CStack[- (ts->CStackPtr - 3)]))
                        {
                            ts->AStack[ts->AStackPtr] = MakeValuesCount(ts->ArgCount);
                            ts->AStackPtr += 1;
                        }
                        else
                        {
                            FAssert(ts->CStackPtr >= 2);
                            FAssert(FixnumP(ts->CStack[- (ts->CStackPtr - 1)]));
                            FAssert(ProcedureP(ts->CStack[- (ts->CStackPtr - 2)]));
                            FAssert(VectorP(AsProcedure(ts->CStack[- (ts->CStackPtr - 2)])->Code));

                            FObject cd = AsVector(AsProcedure(
                                    ts->CStack[- (ts->CStackPtr - 2)])->Code)->Vector[
                                    AsFixnum(ts->CStack[- (ts->CStackPtr - 1)])];
                            if (InstructionP(cd) == 0 || InstructionOpcode(cd)
                                    != DiscardResultOpcode)
                               RaiseExceptionC(R.Assertion, "values",
                                       "values: caller not expecting multiple values",
                                       List(AsProcedure(ts->CStack[- (ts->CStackPtr - 2)])->Name));

                            if (ts->ArgCount == 0)
                            {
                                ts->AStack[ts->AStackPtr] = NoValueObject;
                                ts->AStackPtr += 1;
                            }
                            else
                            {
                                FAssert(ts->AStackPtr >= ts->ArgCount);

                                ts->AStackPtr -= (ts->ArgCount - 1);
                            }
                        }
                    }
                    break;

                case ApplyOpcode:
                {
                    if (ts->ArgCount < 2)
                       RaiseExceptionC(R.Assertion, "apply",
                               "apply: expected at least two arguments", EmptyListObject);

                    FObject prc = ts->AStack[ts->AStackPtr - ts->ArgCount];
                    FObject lst = ts->AStack[ts->AStackPtr - 1];

                    int adx = ts->ArgCount;
                    while (adx > 2)
                    {
                        ts->AStack[ts->AStackPtr - adx] = ts->AStack[ts->AStackPtr - adx + 1];
                        adx -= 1;
                    }

                    ts->ArgCount -= 2;
                    ts->AStackPtr -= 2;

                    FObject ptr = lst;
                    while (PairP(ptr))
                    {
                        ts->AStack[ts->AStackPtr] = First(ptr);
                        ts->AStackPtr += 1;
                        ts->ArgCount += 1;
                        ptr = Rest(ptr);
                    }

                    if (ptr != EmptyListObject)
                       RaiseExceptionC(R.Assertion, "apply", "apply: expected a proper list",
                               List(lst));

                    ts->AStack[ts->AStackPtr] = prc;
                    ts->AStackPtr += 1;
                    break;
                }

                case CaseLambdaOpcode:
                    int cc = InstructionArg(obj);
                    int idx = 0;

                    while (cc > 0)
                    {
                        FAssert(VectorP(AsProcedure(ts->Proc)->Code));
                        FAssert(ts->IP + idx >= 0);
                        FAssert(ts->IP + idx < (int) VectorLength(AsProcedure(ts->Proc)->Code));

                        FObject prc = AsVector(AsProcedure(ts->Proc)->Code)->Vector[ts->IP + idx];

                        FAssert(ProcedureP(prc));

                        if ((AsProcedure(prc)->RestArg == TrueObject
                                && ts->ArgCount + 1 >= ProcedureArgCount(prc))
                                || ProcedureArgCount(prc) == ts->ArgCount)
                        {
                            ts->Proc = prc;
                            FAssert(VectorP(AsProcedure(ts->Proc)->Code));

                            ts->IP = 0;
                            ts->Frame = NoValueObject;

                            break;
                        }

                        idx += 1;
                        cc -= 1;
                    }

                    if (cc == 0)
                        RaiseExceptionC(R.Assertion, "case-lambda",
                                "case-lambda: no matching case", List(MakeFixnum(ts->ArgCount)));
                    break;

                }
            }
        }
    }
    catch (FObject obj)
    {
        throw obj;
    }
}

#define ParameterP(obj) (ProcedureP(obj) && AsProcedure(obj)->Reserved & PROCEDURE_FLAG_PARAMETER)

Define("get-parameter", GetParameterPrimitive)(int argc, FObject argv[])
{
    if (argc != 2)
        RaiseExceptionC(R.Assertion, "get-parameter", "get-parameter: expected two arguments",
                EmptyListObject);

    if (ParameterP(argv[0]) == 0)
        RaiseExceptionC(R.Assertion, "get-parameter", "get-parameter: expected a parameter",
                List(argv[0]));

    if (PairP(argv[1]) == 0)
        RaiseExceptionC(R.Assertion, "get-parameter", "get-parameter: expected a pair",
                List(argv[1]));

    FObject lst = DynamicEnvironment;
    while (PairP(lst))
    {
        FAssert(PairP(First(lst)));

        if (First(First(lst)) == argv[0])
            return(Rest(First(lst)));

        lst = Rest(lst);
    }

    FAssert(lst == EmptyListObject);

    return(Rest(argv[1]));
}

Define("set-parameter!", SetParameterPrimitive)(int argc, FObject argv[])
{
    if (argc != 3)
        RaiseExceptionC(R.Assertion, "set-parameter!", "set-parameter!: expected three arguments",
                EmptyListObject);

    if (ParameterP(argv[0]) == 0)
        RaiseExceptionC(R.Assertion, "set-parameter!", "set-parameter!: expected a parameter",
                List(argv[0]));

    if (PairP(argv[1]) == 0)
        RaiseExceptionC(R.Assertion, "set-parameter!", "set-parameter!: expected a pair",
                List(argv[1]));

    FObject lst = DynamicEnvironment;
    while (PairP(lst))
    {
        FAssert(PairP(First(lst)));

        if (First(First(lst)) == argv[0])
        {
//            AsPair(First(lst))->Rest = argv[2];
            SetRest(First(lst), argv[2]);
            return(NoValueObject);
        }

        lst = Rest(lst);
    }

    FAssert(lst == EmptyListObject);

//    AsPair(argv[1])->Rest = argv[2];
    SetRest(argv[1], argv[2]);
    return(NoValueObject);
}

Define("procedure->parameter", ProcedureToParameterPrimitive)(int argc, FObject argv[])
{
    if (argc != 1)
        RaiseExceptionC(R.Assertion, "procedure->parameter",
                "procedure->parameter: expected one argument", EmptyListObject);

    if (ProcedureP(argv[0]) == 0)
         RaiseExceptionC(R.Assertion, "procedure->parameter",
                 "procedure->parameter: expected a procedure", List(argv[0]));

    AsProcedure(argv[0])->Reserved |= PROCEDURE_FLAG_PARAMETER;

    return(NoValueObject);
}

static FPrimitive * Primitives[] =
{
    &GetParameterPrimitive,
    &SetParameterPrimitive,
    &ProcedureToParameterPrimitive
};

void SetupExecute()
{
    FObject v[2];

    R.WrongNumberOfArguments = MakeStringC("wrong number of arguments");
    R.NotCallable = MakeStringC("not callable");
    R.UnexpectedNumberOfValues = MakeStringC("unexpected number of values");
    R.UndefinedMessage = MakeStringC("variable is undefined");

    for (int idx = 0; idx < sizeof(Primitives) / sizeof(FPrimitive *); idx++)
        DefinePrimitive(R.Bedrock, R.BedrockLibrary, Primitives[idx]);

    DynamicEnvironment = EmptyListObject;

    v[0] = MakeInstruction(ValuesOpcode, 0);
    v[1] = MakeInstruction(ReturnOpcode, 0);

    LibraryExport(R.BedrockLibrary,
            EnvironmentSetC(R.Bedrock, "values", MakeProcedure(StringCToSymbol("values"),
            MakeVector(2, v, NoValueObject), 1, TrueObject)));

    v[0] = MakeInstruction(ApplyOpcode, 0);
    v[1] = MakeInstruction(TailCallOpcode, 0);

    LibraryExport(R.BedrockLibrary,
            EnvironmentSetC(R.Bedrock, "apply", MakeProcedure(StringCToSymbol("apply"),
            MakeVector(2, v, NoValueObject), 2, TrueObject)));
}
