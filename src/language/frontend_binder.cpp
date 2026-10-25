#include "frontend_internal.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace dbc::language::detail
{
    namespace
    {
        struct VariableSymbol
        {
            Type ValueType{};
            StorageClass Storage = StorageClass::Implicit;
            std::size_t ArrayRank = 0;
            SourceSpan Declaration{};
        };

        struct FunctionSymbol
        {
            FunctionDeclaration* Declaration = nullptr;
        };

        class Binder
        {
          public:
            Binder(Program& Value, const FrontendOptions& Options)
                : Program_(Value), Source_(*Value.Source), Options_(Options)
            {
            }

            void Run()
            {
                DeclareTypes();
                DeclareFunctions();
                Labels_.clear();
                DeclareLabels(Program_.Statements);
                BindConstants();
                InferReturnTypes();
                HoistArrays(Program_.Statements, StorageClass::Dim, Globals_);
                for (FunctionDeclaration& Function : Program_.Functions)
                    HoistArrays(Function.Body, StorageClass::Dim, Globals_);
                BindStatements(Program_.Statements);
                for (FunctionDeclaration& Function : Program_.Functions)
                {
                    BindFunction(Function);
                }
            }

          private:
            void DeclareTypes()
            {
                for (TypeDeclaration& Declaration : Program_.Types)
                {
                    if (Source_.Profile != LanguageProfile::DbPro)
                    {
                        Error("DBC2001", "TYPE declarations require the DBPro profile", Declaration.Span);
                    }
                    const std::string Name = Canonical(Declaration.Name);
                    const auto [Iterator, Inserted] = Types_.emplace(Name, &Declaration);
                    if (!Inserted)
                    {
                        Duplicate("DBC2002", "Duplicate TYPE declaration '" + Name + "'", Declaration.Span,
                                  Iterator->second->Span);
                    }
                }
                for (TypeDeclaration& Declaration : Program_.Types)
                {
                    std::unordered_map<std::string, SourceSpan> Fields;
                    for (FieldDeclaration& Field : Declaration.Fields)
                    {
                        const std::string Name = Canonical(Field.Name);
                        const auto [Iterator, Inserted] = Fields.emplace(Name, Field.Span);
                        if (!Inserted)
                        {
                            Duplicate("DBC2003", "Duplicate field '" + Name + "'", Field.Span, Iterator->second);
                        }
                        ResolveDeclaredType(Field.DeclaredType, Field.Span);
                    }
                }
            }

            void DeclareFunctions()
            {
                for (FunctionDeclaration& Declaration : Program_.Functions)
                {
                    const std::string Name = Canonical(Declaration.Name);
                    const auto [Iterator, Inserted] = Functions_.emplace(Name, FunctionSymbol{&Declaration});
                    if (!Inserted)
                    {
                        Duplicate("DBC2004", "Duplicate FUNCTION declaration '" + Name + "'", Declaration.Span,
                                  Iterator->second.Declaration->Span);
                    }
                    if (FindSymbol(Source_.Profile, SymbolKind::Function, Name) != nullptr)
                    {
                        Error("DBC2005", "FUNCTION '" + Name + "' conflicts with a built-in function",
                              Declaration.Span);
                    }
                    ResolveDeclaredType(Declaration.ReturnType, Declaration.Span);
                    for (ParameterDeclaration& Parameter : Declaration.Parameters)
                    {
                        ResolveDeclaredType(Parameter.DeclaredType, Parameter.Span);
                    }
                }
            }

            void DeclareLabels(const std::vector<Statement>& Statements)
            {
                for (const Statement& Value : Statements)
                {
                    if (Value.Kind == StatementKind::Label)
                    {
                        const auto [Iterator, Inserted] = Labels_.emplace(Canonical(Value.Name), Value.Span);
                        if (!Inserted)
                        {
                            Duplicate("DBC2006", "Duplicate label '" + Canonical(Value.Name) + "'", Value.Span,
                                      Iterator->second);
                        }
                    }
                    DeclareLabels(Value.Body);
                    DeclareLabels(Value.ElseBody);
                    for (const CaseClause& Clause : Value.Cases)
                        DeclareLabels(Clause.Body);
                }
            }

            void BindConstants()
            {
                for (ConstantDeclaration& Declaration : Program_.Constants)
                {
                    if (Source_.Profile != LanguageProfile::DbPro)
                    {
                        Error("DBC2007", "#CONSTANT requires the DBPro profile", Declaration.Span);
                    }
                    const std::string Name = Canonical(Declaration.Name);
                    if (Constants_.contains(Name))
                    {
                        Duplicate("DBC2008", "Duplicate constant '" + Name + "'", Declaration.Span,
                                  ConstantSpans_.at(Name));
                        continue;
                    }
                    BindingConstant_ = true;
                    const Type ValueType = BindExpression(*Declaration.Value);
                    BindingConstant_ = false;
                    if (ValueType.Kind != TypeKind::Error)
                    {
                        Constants_.emplace(Name, ValueType);
                        ConstantSpans_.emplace(Name, Declaration.Span);
                    }
                }
            }

            // The return contract is established before calls are bound so recursive functions see one
            // immutable type. Every return site participates; choosing one site silently truncated values.
            void InferReturnTypes()
            {
                CollectDeclaredScalars(Program_.Statements, DeclaredGlobals_, StorageClass::Global);
                for (FunctionDeclaration& Function : Program_.Functions)
                    CollectDeclaredScalars(Function.Body, DeclaredGlobals_, StorageClass::Global);
                CollectDeclaredArrays(Program_.Statements);
                for (FunctionDeclaration& Function : Program_.Functions)
                    CollectDeclaredArrays(Function.Body);
                for (FunctionDeclaration& Function : Program_.Functions)
                    InferReturnType(Function);
            }

            void CollectDeclaredScalars(const std::vector<Statement>& Statements,
                                        std::unordered_map<std::string, Type>& Output, StorageClass Storage)
            {
                for (const Statement& Value : Statements)
                {
                    if (Value.Kind == StatementKind::Declaration && Value.Storage == Storage && Value.ArrayRank == 0)
                        Output.emplace(Canonical(Value.Name), Value.DeclaredType);
                    CollectDeclaredScalars(Value.Body, Output, Storage);
                    CollectDeclaredScalars(Value.ElseBody, Output, Storage);
                    for (const CaseClause& Clause : Value.Cases)
                        CollectDeclaredScalars(Clause.Body, Output, Storage);
                }
            }

            void CollectDeclaredArrays(const std::vector<Statement>& Statements)
            {
                for (const Statement& Value : Statements)
                {
                    if (Value.Kind == StatementKind::Declaration && Value.ArrayRank != 0)
                        DeclaredArrays_.emplace(Canonical(Value.Name), Value.DeclaredType);
                    CollectDeclaredArrays(Value.Body);
                    CollectDeclaredArrays(Value.ElseBody);
                    for (const CaseClause& Clause : Value.Cases)
                        CollectDeclaredArrays(Clause.Body);
                }
            }

            void CollectExitFunctionValues(const std::vector<Statement>& Statements,
                                           std::vector<const Expression*>& Output) const
            {
                for (const Statement& Value : Statements)
                {
                    if (Value.Kind == StatementKind::ExitFunction && !Value.Expressions.empty())
                        Output.push_back(Value.Expressions.front().get());
                    for (const auto* Block : {&Value.Body, &Value.ElseBody})
                        CollectExitFunctionValues(*Block, Output);
                    for (const CaseClause& Clause : Value.Cases)
                        CollectExitFunctionValues(Clause.Body, Output);
                }
            }

            Type InferReturnType(FunctionDeclaration& Function)
            {
                auto& State = Inference_[&Function];
                if (State != InferenceState::Pending)
                    return Function.ReturnType;
                State = InferenceState::InProgress;

                std::vector<const Expression*> Returns;
                CollectExitFunctionValues(Function.Body, Returns);
                if (Function.FinalReturn)
                    Returns.push_back(Function.FinalReturn.get());

                Type Inferred{TypeKind::Void, {}};
                if (!Returns.empty())
                {
                    std::unordered_map<std::string, Type> Locals;
                    for (const ParameterDeclaration& Parameter : Function.Parameters)
                        Locals.emplace(Canonical(Parameter.Name), Parameter.DeclaredType);
                    CollectDeclaredScalars(Function.Body, Locals, StorageClass::Local);
                    for (const Expression* Return : Returns)
                    {
                        Type Current = InferType(*Return, Locals);
                        if (Current.Kind == TypeKind::Boolean)
                            Current = {TypeKind::Integer, {}};
                        if (Current.Kind == TypeKind::Error || Current.Kind == TypeKind::Void)
                            continue;
                        if (Inferred.Kind == TypeKind::Void)
                        {
                            Inferred = Current;
                            continue;
                        }
                        if (Inferred.IsNumeric() && Current.IsNumeric())
                        {
                            if (Source_.Profile == LanguageProfile::DbPro && Inferred.Kind != Current.Kind)
                            {
                                Error("DBC2041", "DBPro FUNCTION return values must have one numeric type",
                                      Return->Span);
                                Inferred = {TypeKind::Error, {}};
                                break;
                            }
                            if (Inferred.Kind == TypeKind::Real || Current.Kind == TypeKind::Real)
                                Inferred = {TypeKind::Real, {}};
                            else
                                Inferred = {TypeKind::Integer, {}};
                            continue;
                        }
                        if (Inferred != Current)
                        {
                            Error("DBC2041", "FUNCTION return values must have one compatible type", Return->Span);
                            Inferred = {TypeKind::Error, {}};
                            break;
                        }
                    }
                }

                const bool StringName = !Function.Name.empty() && Function.Name.back() == '$';
                if (Source_.Profile == LanguageProfile::DbPro)
                {
                    if (StringName)
                    {
                        Function.ReturnType = {TypeKind::String, {}};
                        if (Inferred.Kind != TypeKind::Void && Inferred.Kind != TypeKind::Error &&
                            Inferred.Kind != TypeKind::String)
                        {
                            Error("DBC2041", "A DBPro FUNCTION ending in '$' must return a string", Function.Span);
                        }
                    }
                    else if (Inferred.Kind == TypeKind::String)
                    {
                        Function.ReturnType = {TypeKind::Integer, {}};
                        Error("DBC2041", "A DBPro string FUNCTION name must end in '$'", Function.Span);
                    }
                    else if (Inferred.Kind != TypeKind::Void && Inferred.Kind != TypeKind::Error)
                    {
                        Function.ReturnType = Inferred;
                    }
                }
                else if (Inferred.Kind != TypeKind::Void && Inferred.Kind != TypeKind::Error)
                {
                    Function.ReturnType = Inferred;
                }
                State = InferenceState::Done;
                return Function.ReturnType;
            }

            Type InferType(const Expression& Value, const std::unordered_map<std::string, Type>& Locals)
            {
                switch (Value.Kind)
                {
                    case ExpressionKind::IntegerLiteral:
                        return {TypeKind::Integer, {}};
                    case ExpressionKind::RealLiteral:
                        return {TypeKind::Real, {}};
                    case ExpressionKind::StringLiteral:
                        return {TypeKind::String, {}};
                    case ExpressionKind::Name:
                    {
                        const std::string Name = Canonical(Value.Text);
                        if (const auto Local = Locals.find(Name); Local != Locals.end())
                            return Local->second;
                        if (const auto Constant = Constants_.find(Name); Constant != Constants_.end())
                            return Constant->second;
                        if (const auto Global = DeclaredGlobals_.find(Name); Global != DeclaredGlobals_.end())
                            return Global->second;
                        return SuffixType(Name);
                    }
                    case ExpressionKind::Unary:
                    {
                        if (Value.Children.empty())
                            return {TypeKind::Error, {}};
                        if (Value.Text == "NOT")
                            return {TypeKind::Integer, {}};
                        return InferType(*Value.Children.front(), Locals);
                    }
                    case ExpressionKind::Binary:
                    {
                        if (Value.Children.size() != 2)
                            return {TypeKind::Error, {}};
                        const Type Left = InferType(*Value.Children[0], Locals);
                        const Type Right = InferType(*Value.Children[1], Locals);
                        const bool Logical = Value.Text == "=" || Value.Text == "<>" || Value.Text == "<" ||
                                             Value.Text == "<=" || Value.Text == ">" || Value.Text == ">=" ||
                                             Value.Text == "AND" || Value.Text == "OR";
                        if (Logical)
                            return {TypeKind::Integer, {}};
                        if (Value.Text == "XOR" || Value.Text == "<<" || Value.Text == ">>" || Value.Text == "&&" ||
                            Value.Text == "||" || Value.Text == "~~")
                            return {TypeKind::Integer, {}};
                        if (Left.Kind == TypeKind::String || Right.Kind == TypeKind::String)
                            return Value.Text == "+" && Left.Kind == Right.Kind ? Type{TypeKind::String, {}}
                                                                                : Type{TypeKind::Error, {}};
                        if (Left.Kind == TypeKind::Real || Right.Kind == TypeKind::Real)
                            return {TypeKind::Real, {}};
                        if (Left.Kind == TypeKind::Error || Right.Kind == TypeKind::Error)
                            return {TypeKind::Error, {}};
                        return {TypeKind::Integer, {}};
                    }
                    case ExpressionKind::CallOrIndex:
                    case ExpressionKind::Call:
                    case ExpressionKind::Index:
                    {
                        const std::string Name = Canonical(Value.Text);
                        if (const auto Array = DeclaredArrays_.find(Name); Array != DeclaredArrays_.end())
                            return Array->second;
                        if (const auto Function = Functions_.find(Name); Function != Functions_.end())
                            return InferReturnType(*Function->second.Declaration);
                        return TypeFromSuffix(Name);
                    }
                    case ExpressionKind::Member:
                    {
                        if (Value.Children.empty())
                            return {TypeKind::Error, {}};
                        const Type Base = InferType(*Value.Children.front(), Locals);
                        if (Base.Kind == TypeKind::UserDefined)
                        {
                            if (const auto Declared = Types_.find(Canonical(Base.Name)); Declared != Types_.end())
                            {
                                const std::string FieldName = Canonical(Value.Text);
                                for (const FieldDeclaration& Field : Declared->second->Fields)
                                {
                                    if (Canonical(Field.Name) == FieldName)
                                        return Field.DeclaredType;
                                }
                            }
                        }
                        return SuffixType(Canonical(Value.Text));
                    }
                    case ExpressionKind::Error:
                        break;
                }
                return {TypeKind::Error, {}};
            }

            static Type SuffixType(const std::string& Name)
            {
                if (!Name.empty() && Name.back() == '$')
                    return {TypeKind::String, {}};
                if (!Name.empty() && Name.back() == '#')
                    return {TypeKind::Real, {}};
                return {TypeKind::Integer, {}};
            }

            void BindFunction(FunctionDeclaration& Function)
            {
                std::unordered_map<std::string, VariableSymbol> Locals;
                Labels_.clear();
                DeclareLabels(Function.Body);
                CurrentLocals_ = &Locals;
                CurrentFunction_ = &Function;
                for (ParameterDeclaration& Parameter : Function.Parameters)
                {
                    if (Parameter.HasExplicitType && Source_.Profile != LanguageProfile::DbPro)
                    {
                        Error("DBC2045", "Typed FUNCTION parameters require the DBPro profile", Parameter.Span);
                    }
                    ResolveDeclaredType(Parameter.DeclaredType, Parameter.Span);
                    const std::string Name = Canonical(Parameter.Name);
                    const auto [Iterator, Inserted] = Locals.emplace(
                        Name, VariableSymbol{Parameter.DeclaredType, StorageClass::Parameter, 0, Parameter.Span});
                    if (!Inserted)
                    {
                        Duplicate("DBC2009", "Duplicate parameter '" + Name + "'", Parameter.Span,
                                  Iterator->second.Declaration);
                    }
                }
                HoistArrays(Function.Body, StorageClass::Local, Locals);
                BindStatements(Function.Body);
                if (Function.FinalReturn)
                {
                    const Type Return = BindExpression(*Function.FinalReturn);
                    RequireConversion(Return, Function.ReturnType, Function.FinalReturn->Span, "function return");
                }
                CurrentFunction_ = nullptr;
                CurrentLocals_ = nullptr;
            }

            void BindStatements(std::vector<Statement>& Statements)
            {
                for (Statement& Value : Statements)
                {
                    BindStatement(Value);
                }
            }

            void BindStatement(Statement& Value)
            {
                switch (Value.Kind)
                {
                    case StatementKind::Error:
                    case StatementKind::Label:
                    case StatementKind::End:
                        return;
                    case StatementKind::Command:
                        BindCommand(Value);
                        return;
                    case StatementKind::Expression:
                        for (auto& Expression : Value.Expressions)
                            BindExpression(*Expression);
                        return;
                    case StatementKind::Assignment:
                        BindAssignment(Value);
                        return;
                    case StatementKind::Declaration:
                        BindDeclaration(Value);
                        return;
                    case StatementKind::For:
                        BindFor(Value);
                        return;
                    case StatementKind::While:
                    case StatementKind::Repeat:
                    case StatementKind::If:
                        if (Value.Builtin)
                        {
                            const SymbolDescriptor& Descriptor = Symbol(*Value.Builtin);
                            if (!AvailableIn(Descriptor, Source_.Profile))
                            {
                                Error("DBC2014",
                                      "Statement '" + std::string(Descriptor.CanonicalName) + "' is not available in " +
                                          std::string(ProfileName(Source_.Profile)),
                                      Value.Span);
                            }
                            else
                            {
                                CheckSupport(Descriptor, Value.Span);
                            }
                        }
                        if (!Value.Expressions.empty())
                            RequireCondition(BindExpression(*Value.Expressions.front()));
                        BindStatements(Value.Body);
                        BindStatements(Value.ElseBody);
                        return;
                    case StatementKind::Do:
                        BindStatements(Value.Body);
                        return;
                    case StatementKind::Select:
                        BindSelect(Value);
                        return;
                    case StatementKind::Goto:
                    case StatementKind::Gosub:
                        if (!Labels_.contains(Canonical(Value.Name)))
                        {
                            Error("DBC2010", "Unknown label '" + Canonical(Value.Name) + "'", Value.Span);
                        }
                        return;
                    case StatementKind::Return:
                        if (CurrentFunction_)
                        {
                            Error("DBC2011", "RETURN cannot leave a FUNCTION; use EXITFUNCTION", Value.Span);
                        }
                        return;
                    case StatementKind::Exit:
                        return;
                    case StatementKind::ExitFunction:
                        if (!CurrentFunction_)
                        {
                            Error("DBC2012", "EXITFUNCTION is only valid inside a FUNCTION", Value.Span);
                        }
                        else if (!Value.Expressions.empty())
                        {
                            RequireConversion(BindExpression(*Value.Expressions.front()), CurrentFunction_->ReturnType,
                                              Value.Expressions.front()->Span, "EXITFUNCTION value");
                        }
                        return;
                    case StatementKind::Data:
                        BindCommand(Value);
                        return;
                }
            }

            void BindCommand(Statement& Value)
            {
                const SymbolDescriptor* Descriptor = nullptr;
                if (Value.Builtin)
                {
                    Descriptor = &Symbol(*Value.Builtin);
                }
                else
                {
                    Descriptor = FindSymbol(Source_.Profile, SymbolKind::Command, Value.Name);
                }
                if (!Descriptor)
                {
                    const std::string Name = Canonical(Value.Name);
                    const auto Function = Functions_.find(Name);
                    if (Source_.Profile == LanguageProfile::Dbc && Value.Expressions.empty() &&
                        Function != Functions_.end() && Function->second.Declaration->Parameters.empty())
                    {
                        auto Call = std::make_unique<Expression>();
                        Call->Kind = ExpressionKind::CallOrIndex;
                        Call->Span = Value.Span;
                        Call->Text = Name;
                        Value.Kind = StatementKind::Expression;
                        Value.Expressions.push_back(std::move(Call));
                        BindExpression(*Value.Expressions.front());
                        return;
                    }
                    const LanguageProfile Other =
                        Source_.Profile == LanguageProfile::Dbc ? LanguageProfile::DbPro : LanguageProfile::Dbc;
                    if (const SymbolDescriptor* OtherDescriptor = FindSymbol(Other, SymbolKind::Command, Value.Name))
                    {
                        Error("DBC2014",
                              "Command '" + std::string(OtherDescriptor->CanonicalName) + "' is not available in " +
                                  std::string(ProfileName(Source_.Profile)),
                              Value.Span);
                        for (auto& Expression : Value.Expressions)
                            BindExpression(*Expression);
                        return;
                    }
                    Error("DBC2013", "Unknown command '" + Canonical(Value.Name) + "'", Value.Span);
                    for (auto& Expression : Value.Expressions)
                        BindExpression(*Expression);
                    return;
                }
                Value.Builtin = Descriptor->Id;
                if (!AvailableIn(*Descriptor, Source_.Profile))
                {
                    Error("DBC2014",
                          "Command '" + std::string(Descriptor->CanonicalName) + "' is not available in " +
                              std::string(ProfileName(Source_.Profile)),
                          Value.Span);
                }
                else
                {
                    CheckSupport(*Descriptor, Value.Span);
                    CheckArity(*Descriptor, Value.Expressions.size(), Value.Span);
                }
                const BuiltinOverload* Selected =
                    ExecutableIn(*Descriptor, Source_.Profile)
                        ? FindOverload(Descriptor->Id, Source_.Profile, static_cast<int>(Value.Expressions.size()))
                        : nullptr;
                if (Selected)
                    Value.Overload = Selected->Id;
                const BuiltinConvention Convention = ConventionFor(Descriptor->Id);
                if (Convention == BuiltinConvention::DataRead)
                {
                    for (auto& Expression : Value.Expressions)
                        BindLValue(*Expression);
                    ValidateOverload(Selected, Value.Expressions, Value.Span);
                    return;
                }
                if (Convention == BuiltinConvention::DataRestore)
                {
                    for (auto& Expression : Value.Expressions)
                    {
                        if (Expression->Kind != ExpressionKind::Name &&
                            Expression->Kind != ExpressionKind::IntegerLiteral)
                        {
                            Error("DBC2047", "RESTORE requires a DATA label or non-negative constant index",
                                  Expression->Span);
                        }
                    }
                    return;
                }
                if (Convention == BuiltinConvention::Input || Convention == BuiltinConvention::FileRead)
                {
                    if (Value.Expressions.empty())
                        return;
                    for (std::size_t Index = 0; Index + 1 < Value.Expressions.size(); ++Index)
                        BindExpression(*Value.Expressions[Index]);
                    BindLValue(*Value.Expressions.back());
                    ValidateOverload(Selected, Value.Expressions, Value.Span);
                    return;
                }
                if (Convention == BuiltinConvention::LoadArray || Convention == BuiltinConvention::SaveArray)
                {
                    BindArrayCommand(Value);
                    ValidateOverload(Selected, Value.Expressions, Value.Span);
                    return;
                }
                if (Convention == BuiltinConvention::Undim)
                {
                    BindArrayMarker(Value, false);
                    ValidateOverload(Selected, Value.Expressions, Value.Span);
                    return;
                }
                if (Convention == BuiltinConvention::ArrayCollection)
                {
                    if (!Value.Expressions.empty())
                        BindArrayMarker(*Value.Expressions.front(), false);
                    for (std::size_t Index = 1; Index < Value.Expressions.size(); ++Index)
                        BindExpression(*Value.Expressions[Index]);
                    ValidateOverload(Selected, Value.Expressions, Value.Span);
                    return;
                }
                for (auto& Expression : Value.Expressions)
                    BindExpression(*Expression);
                ValidateOverload(Selected, Value.Expressions, Value.Span);
            }

            void BindArrayCommand(Statement& Value)
            {
                if (Value.Expressions.size() != 2)
                    return;
                BindExpression(*Value.Expressions.front());
                BindArrayMarker(Value, true);
            }

            void BindArrayMarker(Statement& Value, bool RequireRankMarkers)
            {
                if (Value.Expressions.empty())
                    return;
                BindArrayMarker(*Value.Expressions.back(), RequireRankMarkers);
            }

            void BindArrayMarker(Expression& Array, bool RequireRankMarkers)
            {
                if (Array.Kind != ExpressionKind::CallOrIndex && Array.Kind != ExpressionKind::Index)
                {
                    Error("DBC2048", "Array command requires an array name followed by parentheses", Array.Span);
                    BindExpression(Array);
                    return;
                }
                const std::string Name = Canonical(Array.Text);
                VariableSymbol* Variable = FindVariable(ArrayKey(Name));
                if (!Variable || Variable->ArrayRank == 0)
                {
                    Error("DBC2048", "Unknown array '" + Name + "'", Array.Span);
                    BindExpression(Array);
                    return;
                }
                // The historical syntax is `name(index numbers)`: the markers are evaluated but their
                // count does not have to match the rank (`LOAD ARRAY "f", map(0)` on a 2-D array).
                (void)RequireRankMarkers;
                Array.Kind = ExpressionKind::Index;
                Array.Reference = ReferenceKind::Array;
                Array.ResolvedType = Variable->ValueType;
                Array.ArrayRank = Variable->ArrayRank;
                for (auto& Index : Array.Children)
                {
                    const Type IndexType = BindExpression(*Index);
                    if (!IndexType.IsNumeric() && IndexType.Kind != TypeKind::Error)
                        Error("DBC2032", "Array marker indices must be numeric", Index->Span);
                }
            }

            void BindAssignment(Statement& Value)
            {
                if (Value.Expressions.empty())
                    return;
                Type Target = BindLValue(*Value.Expressions.front());
                if (Value.Name == "INC" || Value.Name == "DEC")
                {
                    if (!Target.IsNumeric())
                    {
                        Error("DBC2015", Value.Name + " requires a numeric target", Value.Expressions.front()->Span);
                    }
                    if (Value.Expressions.size() > 1)
                    {
                        const Type Amount = BindExpression(*Value.Expressions[1]);
                        if (!Amount.IsNumeric() && Amount.Kind != TypeKind::Error)
                            Error("DBC2016", Value.Name + " amount must be numeric", Value.Expressions[1]->Span);
                    }
                    return;
                }
                if (Value.Expressions.size() > 1)
                {
                    const Type Source = BindExpression(*Value.Expressions[1]);
                    RequireConversion(Source, Target, Value.Expressions[1]->Span, "assignment");
                }
            }

            void BindDeclaration(Statement& Value)
            {
                if (Value.HasExplicitType && Source_.Profile != LanguageProfile::DbPro)
                {
                    Error("DBC2046", "AS type declarations require the DBPro profile", Value.Span);
                }
                if ((Value.Storage == StorageClass::Local || Value.Storage == StorageClass::Global) &&
                    Source_.Profile != LanguageProfile::DbPro)
                {
                    Error("DBC2017", "LOCAL and GLOBAL declarations require the DBPro profile", Value.Span);
                }
                if (Value.Storage == StorageClass::Local && !CurrentFunction_)
                {
                    Error("DBC2018", "LOCAL is only valid inside a FUNCTION", Value.Span);
                }
                ResolveDeclaredType(Value.DeclaredType, Value.Span);
                auto* Table = Value.Storage == StorageClass::Local ? CurrentLocals_ : &Globals_;
                if (!Table)
                {
                    Table = &Globals_;
                }
                const std::string Name = Canonical(Value.Name);
                const std::string Key = Value.ArrayRank != 0 ? ArrayKey(Name) : Name;
                const auto [Iterator, Inserted] =
                    Table->emplace(Key, VariableSymbol{Value.DeclaredType, Value.Storage, Value.ArrayRank, Value.Span});
                if (!Inserted && !SameSpan(Iterator->second.Declaration, Value.Span))
                {
                    const bool GlobalReference = CurrentFunction_ && Value.Storage == StorageClass::Global &&
                                                 Value.ArrayRank == 0 && Value.Expressions.empty() &&
                                                 Iterator->second.ArrayRank == 0 &&
                                                 Iterator->second.ValueType == Value.DeclaredType;
                    if (!GlobalReference)
                    {
                        Duplicate("DBC2019", "Duplicate variable declaration '" + Name + "'", Value.Span,
                                  Iterator->second.Declaration);
                    }
                }
                for (std::size_t Index = 0; Index < Value.ArrayRank && Index < Value.Expressions.size(); ++Index)
                {
                    const Type Dimension = BindExpression(*Value.Expressions[Index]);
                    if (!Dimension.IsNumeric() && Dimension.Kind != TypeKind::Error)
                        Error("DBC2020", "Array dimensions must be numeric", Value.Expressions[Index]->Span);
                }
                if (Value.Expressions.size() > Value.ArrayRank)
                {
                    const Type Initializer = BindExpression(*Value.Expressions.back());
                    RequireConversion(Initializer, Value.DeclaredType, Value.Expressions.back()->Span, "initializer");
                }
            }

            void BindFor(Statement& Value)
            {
                VariableSymbol& Variable = ResolveOrCreate(Value.Name, Value.Span);
                if (!Variable.ValueType.IsNumeric())
                {
                    Error("DBC2021", "FOR variable must be numeric", Value.Span);
                }
                for (auto& Expression : Value.Expressions)
                {
                    const Type Bound = BindExpression(*Expression);
                    if (!Bound.IsNumeric() && Bound.Kind != TypeKind::Error)
                        Error("DBC2022", "FOR bounds and STEP must be numeric", Expression->Span);
                }
                BindStatements(Value.Body);
            }

            void BindSelect(Statement& Value)
            {
                if (Value.Expressions.empty())
                    return;
                const Type Selector = BindExpression(*Value.Expressions.front());
                bool SawDefault = false;
                for (CaseClause& Clause : Value.Cases)
                {
                    if (Clause.IsDefault)
                    {
                        if (SawDefault)
                            Error("DBC2023", "SELECT may contain only one CASE DEFAULT", Clause.Span);
                        SawDefault = true;
                    }
                    for (auto& Expression : Clause.Values)
                    {
                        const Type CaseType = BindExpression(*Expression);
                        if (!CanConvert(CaseType, Selector) && !CanConvert(Selector, CaseType))
                            Error("DBC2024", "CASE value is incompatible with SELECT expression", Expression->Span);
                    }
                    BindStatements(Clause.Body);
                }
            }

            Type BindExpression(Expression& Value)
            {
                switch (Value.Kind)
                {
                    case ExpressionKind::Error:
                        return Value.ResolvedType = {TypeKind::Error, {}};
                    case ExpressionKind::IntegerLiteral:
                        return Value.ResolvedType = {TypeKind::Integer, {}};
                    case ExpressionKind::RealLiteral:
                        return Value.ResolvedType = {TypeKind::Real, {}};
                    case ExpressionKind::StringLiteral:
                        return Value.ResolvedType = {TypeKind::String, {}};
                    case ExpressionKind::Name:
                        return BindName(Value);
                    case ExpressionKind::Unary:
                        return BindUnary(Value);
                    case ExpressionKind::Binary:
                        return BindBinary(Value);
                    case ExpressionKind::CallOrIndex:
                    case ExpressionKind::Call:
                    case ExpressionKind::Index:
                        return BindCallOrIndex(Value, false);
                    case ExpressionKind::Member:
                        return BindMember(Value, false);
                }
                return Value.ResolvedType = {TypeKind::Error, {}};
            }

            Type BindLValue(Expression& Value)
            {
                if (Value.Kind == ExpressionKind::Name)
                {
                    VariableSymbol& Symbol = ResolveOrCreate(Value.Text, Value.Span);
                    Value.Reference = ReferenceKind::Variable;
                    if (Symbol.ArrayRank != 0)
                    {
                        Error("DBC2025", "Array '" + Canonical(Value.Text) + "' requires indices", Value.Span);
                    }
                    return Value.ResolvedType = Symbol.ValueType;
                }
                if (Value.Kind == ExpressionKind::CallOrIndex || Value.Kind == ExpressionKind::Index)
                    return BindCallOrIndex(Value, true);
                if (Value.Kind == ExpressionKind::Member)
                    return BindMember(Value, true);
                Error("DBC2026", "Expression is not assignable", Value.Span);
                BindExpression(Value);
                return {TypeKind::Error, {}};
            }

            Type BindName(Expression& Value)
            {
                const std::string Name = Canonical(Value.Text);
                if (const auto Constant = Constants_.find(Name); Constant != Constants_.end())
                {
                    Value.Reference = ReferenceKind::Constant;
                    return Value.ResolvedType = Constant->second;
                }
                if (BindingConstant_)
                {
                    Error("DBC2027", "Constant expression refers to unknown constant '" + Name + "'", Value.Span);
                    return Value.ResolvedType = {TypeKind::Error, {}};
                }
                Value.Reference = ReferenceKind::Variable;
                return Value.ResolvedType = ResolveOrCreate(Name, Value.Span).ValueType;
            }

            Type BindUnary(Expression& Value)
            {
                if (Value.Children.empty())
                    return Value.ResolvedType = {TypeKind::Error, {}};
                const Type Operand = BindExpression(*Value.Children.front());
                if (Value.Text == "NOT")
                {
                    RequireCondition(Operand, Value.Span);
                    return Value.ResolvedType = {TypeKind::Boolean, {}};
                }
                if (Value.Text == "BITNOT")
                {
                    if (Operand.Kind != TypeKind::Integer && Operand.Kind != TypeKind::Boolean &&
                        Operand.Kind != TypeKind::Error)
                    {
                        Error("DBC2028", "DBPro NOT requires an integer operand", Value.Span);
                        return Value.ResolvedType = {TypeKind::Error, {}};
                    }
                    return Value.ResolvedType = {TypeKind::Integer, {}};
                }
                if (!Operand.IsNumeric() && Operand.Kind != TypeKind::Error)
                {
                    Error("DBC2028", "Unary '" + Value.Text + "' requires a numeric operand", Value.Span);
                    return Value.ResolvedType = {TypeKind::Error, {}};
                }
                return Value.ResolvedType = Operand;
            }

            Type BindBinary(Expression& Value)
            {
                if (Value.Children.size() != 2)
                    return Value.ResolvedType = {TypeKind::Error, {}};
                const Type Left = BindExpression(*Value.Children[0]);
                const Type Right = BindExpression(*Value.Children[1]);
                if (Left.Kind == TypeKind::Error || Right.Kind == TypeKind::Error)
                    return Value.ResolvedType = {TypeKind::Error, {}};
                const bool Comparison = Value.Text == "=" || Value.Text == "<>" || Value.Text == "<" ||
                                        Value.Text == "<=" || Value.Text == ">" || Value.Text == ">=";
                if (Comparison)
                {
                    if (!CanConvert(Left, Right) && !CanConvert(Right, Left))
                        Error("DBC2029", "Comparison operands are incompatible", Value.Span);
                    return Value.ResolvedType = {TypeKind::Boolean, {}};
                }
                if (Value.Text == "AND" || Value.Text == "OR")
                {
                    RequireCondition(Left, Value.Children[0]->Span);
                    RequireCondition(Right, Value.Children[1]->Span);
                    return Value.ResolvedType = {TypeKind::Boolean, {}};
                }
                if (Value.Text == "XOR" || Value.Text == "<<" || Value.Text == ">>" || Value.Text == "&&" ||
                    Value.Text == "||" || Value.Text == "~~")
                {
                    const auto IsIntegerLike = [](TypeKind Kind)
                    { return Kind == TypeKind::Integer || Kind == TypeKind::Boolean; };
                    if (!IsIntegerLike(Left.Kind) || !IsIntegerLike(Right.Kind))
                    {
                        Error("DBC2030", "Operator '" + Value.Text + "' requires integer operands", Value.Span);
                        return Value.ResolvedType = {TypeKind::Error, {}};
                    }
                    return Value.ResolvedType = {TypeKind::Integer, {}};
                }
                if (Value.Text == "+" && Left.Kind == TypeKind::String && Right.Kind == TypeKind::String)
                    return Value.ResolvedType = {TypeKind::String, {}};
                if ((!Left.IsNumeric() || !Right.IsNumeric()) && Left.Kind != TypeKind::Error &&
                    Right.Kind != TypeKind::Error)
                {
                    Error("DBC2030", "Operator '" + Value.Text + "' requires numeric operands", Value.Span);
                    return Value.ResolvedType = {TypeKind::Error, {}};
                }
                return Value.ResolvedType =
                           Type{Left.Kind == TypeKind::Real || Right.Kind == TypeKind::Real ? TypeKind::Real
                                                                                            : TypeKind::Integer,
                                {}};
            }

            Type BindCallOrIndex(Expression& Value, bool LValue)
            {
                const std::string Name = Canonical(Value.Text);
                if (VariableSymbol* Variable = FindVariable(ArrayKey(Name)); Variable && Variable->ArrayRank != 0)
                {
                    Value.Kind = ExpressionKind::Index;
                    Value.Reference = ReferenceKind::Array;
                    Value.ArrayRank = Variable->ArrayRank;
                    const bool CurrentIndex =
                        Source_.Profile == LanguageProfile::DbPro && Variable->ArrayRank == 1 && Value.Children.empty();
                    if (!CurrentIndex && Variable->ArrayRank != Value.Children.size())
                    {
                        Error("DBC2031",
                              "Array '" + Name + "' expects " + std::to_string(Variable->ArrayRank) + " indices, got " +
                                  std::to_string(Value.Children.size()),
                              Value.Span);
                    }
                    for (auto& Index : Value.Children)
                    {
                        const Type IndexType = BindExpression(*Index);
                        if (!IndexType.IsNumeric() && IndexType.Kind != TypeKind::Error)
                            Error("DBC2032", "Array indices must be numeric", Index->Span);
                    }
                    return Value.ResolvedType = Variable->ValueType;
                }
                if (LValue)
                {
                    Error("DBC2033", "Unknown array '" + Name + "'", Value.Span);
                    for (auto& Index : Value.Children)
                        BindExpression(*Index);
                    return Value.ResolvedType = {TypeKind::Error, {}};
                }
                if (const auto Function = Functions_.find(Name); Function != Functions_.end())
                {
                    Value.Kind = ExpressionKind::Call;
                    Value.Reference = ReferenceKind::UserFunction;
                    FunctionDeclaration& Declaration = *Function->second.Declaration;
                    if (Declaration.Parameters.size() != Value.Children.size())
                    {
                        Error("DBC2034",
                              "FUNCTION '" + Name + "' expects " + std::to_string(Declaration.Parameters.size()) +
                                  " arguments, got " + std::to_string(Value.Children.size()),
                              Value.Span);
                    }
                    const std::size_t Count = std::min(Declaration.Parameters.size(), Value.Children.size());
                    for (std::size_t Index = 0; Index < Value.Children.size(); ++Index)
                    {
                        const Type Argument = BindExpression(*Value.Children[Index]);
                        if (Index < Count)
                            RequireConversion(Argument, Declaration.Parameters[Index].DeclaredType,
                                              Value.Children[Index]->Span, "function argument");
                    }
                    return Value.ResolvedType = Declaration.ReturnType;
                }

                const SymbolDescriptor* Descriptor = FindSymbol(Source_.Profile, SymbolKind::Function, Name);
                if (!Descriptor)
                {
                    const LanguageProfile Other =
                        Source_.Profile == LanguageProfile::Dbc ? LanguageProfile::DbPro : LanguageProfile::Dbc;
                    if (const SymbolDescriptor* OtherDescriptor = FindSymbol(Other, SymbolKind::Function, Name))
                    {
                        Error("DBC2036",
                              "Function '" + std::string(OtherDescriptor->CanonicalName) + "' is not available in " +
                                  std::string(ProfileName(Source_.Profile)),
                              Value.Span);
                        for (auto& Argument : Value.Children)
                            BindExpression(*Argument);
                        return Value.ResolvedType = {TypeKind::Error, {}};
                    }
                    Error("DBC2035", "Unknown function or array '" + Name + "'", Value.Span);
                    for (auto& Argument : Value.Children)
                        BindExpression(*Argument);
                    return Value.ResolvedType = {TypeKind::Error, {}};
                }
                Value.Builtin = Descriptor->Id;
                Value.Kind = ExpressionKind::Call;
                Value.Reference = ReferenceKind::BuiltinFunction;
                CheckSupport(*Descriptor, Value.Span);
                CheckArity(*Descriptor, Value.Children.size(), Value.Span);
                const BuiltinOverload* Selected =
                    ExecutableIn(*Descriptor, Source_.Profile)
                        ? FindOverload(Descriptor->Id, Source_.Profile, static_cast<int>(Value.Children.size()))
                        : nullptr;
                if (ConventionFor(Descriptor->Id) == BuiltinConvention::ArrayCollection && !Value.Children.empty())
                {
                    BindArrayMarker(*Value.Children.front(), false);
                    for (std::size_t Index = 1; Index < Value.Children.size(); ++Index)
                        BindExpression(*Value.Children[Index]);
                }
                else
                {
                    for (auto& Argument : Value.Children)
                        BindExpression(*Argument);
                }
                if (!Selected)
                    return Value.ResolvedType = TypeFromSuffix(Name);
                Value.Overload = Selected->Id;
                ValidateOverload(Selected, Value.Children, Value.Span);
                return Value.ResolvedType = TypeForBuiltin(Selected->ReturnType);
            }

            Type BindMember(Expression& Value, bool LValue)
            {
                if (Source_.Profile != LanguageProfile::DbPro)
                {
                    Error("DBC2037", "Member access requires the DBPro profile", Value.Span);
                }
                if (Value.Children.empty())
                    return Value.ResolvedType = {TypeKind::Error, {}};
                const Type Base =
                    LValue ? BindLValue(*Value.Children.front()) : BindExpression(*Value.Children.front());
                if (Base.Kind != TypeKind::UserDefined)
                {
                    if (Base.Kind != TypeKind::Error)
                        Error("DBC2038", "Member access requires a user-defined TYPE value", Value.Span);
                    return Value.ResolvedType = {TypeKind::Error, {}};
                }
                const auto TypeIterator = Types_.find(Canonical(Base.Name));
                if (TypeIterator == Types_.end())
                    return Value.ResolvedType = {TypeKind::Error, {}};
                const std::string FieldName = Canonical(Value.Text);
                const auto Field = std::find_if(
                    TypeIterator->second->Fields.begin(), TypeIterator->second->Fields.end(),
                    [&](const FieldDeclaration& Candidate) { return Canonical(Candidate.Name) == FieldName; });
                if (Field == TypeIterator->second->Fields.end())
                {
                    Error("DBC2039", "TYPE '" + Base.Name + "' has no field '" + FieldName + "'", Value.Span);
                    return Value.ResolvedType = {TypeKind::Error, {}};
                }
                Value.Reference = ReferenceKind::Field;
                return Value.ResolvedType = Field->DeclaredType;
            }

            VariableSymbol& ResolveOrCreate(std::string Name, SourceSpan Span)
            {
                Name = Canonical(Name);
                if (VariableSymbol* Existing = FindVariable(Name))
                    return *Existing;
                const Type Inferred = SuffixType(Name);
                if (CurrentFunction_ && CurrentLocals_)
                {
                    CurrentFunction_->ImplicitLocals.emplace_back(Name, Inferred);
                    return CurrentLocals_->emplace(Name, VariableSymbol{Inferred, StorageClass::Implicit, 0, Span})
                        .first->second;
                }
                return Globals_.emplace(Name, VariableSymbol{Inferred, StorageClass::Implicit, 0, Span}).first->second;
            }

            // Arrays and scalars live in separate namespaces: `x#` and `x#(i)` are unrelated variables.
            static std::string ArrayKey(const std::string& Name)
            {
                return Name + "()";
            }

            static bool SameSpan(const SourceSpan& Left, const SourceSpan& Right)
            {
                return Left.Begin.Offset == Right.Begin.Offset && Left.LogicalPath == Right.LogicalPath;
            }

            // DIM takes effect at run time, so an array is known everywhere in its scope, even before the
            // line that declares it.
            void HoistArrays(const std::vector<Statement>& Statements, StorageClass Storage,
                             std::unordered_map<std::string, VariableSymbol>& Table)
            {
                for (const Statement& Value : Statements)
                {
                    if (Value.Kind == StatementKind::Declaration && Value.ArrayRank != 0 &&
                        (Value.Storage == StorageClass::Local) == (Storage == StorageClass::Local))
                    {
                        Table.emplace(ArrayKey(Canonical(Value.Name)),
                                      VariableSymbol{Value.DeclaredType, Value.Storage, Value.ArrayRank, Value.Span});
                    }
                    HoistArrays(Value.Body, Storage, Table);
                    HoistArrays(Value.ElseBody, Storage, Table);
                    for (const CaseClause& Clause : Value.Cases)
                        HoistArrays(Clause.Body, Storage, Table);
                }
            }

            // Inside a FUNCTION only arrays and GLOBAL declarations are shared with the main program;
            // every other name is a local of that function in both historical dialects.
            VariableSymbol* FindVariable(const std::string& Name)
            {
                if (CurrentLocals_)
                {
                    if (auto Iterator = CurrentLocals_->find(Name); Iterator != CurrentLocals_->end())
                        return &Iterator->second;
                }
                if (auto Iterator = Globals_.find(Name); Iterator != Globals_.end())
                {
                    const bool Shared =
                        Iterator->second.Storage == StorageClass::Global || Iterator->second.ArrayRank != 0;
                    if (!CurrentFunction_ || Shared)
                        return &Iterator->second;
                }
                return nullptr;
            }

            void ResolveDeclaredType(Type& Value, SourceSpan Span)
            {
                if (Value.Kind == TypeKind::UserDefined && !Types_.contains(Canonical(Value.Name)))
                {
                    Error("DBC2040", "Unknown TYPE '" + Canonical(Value.Name) + "'", Span);
                    Value = {TypeKind::Error, {}};
                }
            }

            bool CanConvert(const Type& Source, const Type& Target) const
            {
                if (Source.Kind == TypeKind::Error || Target.Kind == TypeKind::Error)
                    return true;
                if (Source == Target)
                    return true;
                return Source.IsNumeric() && Target.IsNumeric();
            }

            void RequireConversion(const Type& Source, const Type& Target, SourceSpan Span, std::string_view Context)
            {
                if (!CanConvert(Source, Target))
                {
                    Error("DBC2041",
                          "Cannot convert " + TypeName(Source) + " to " + TypeName(Target) + " for " +
                              std::string(Context),
                          Span);
                }
            }

            void RequireCondition(const Type& Value, SourceSpan Span = {})
            {
                if (!Value.IsNumeric() && Value.Kind != TypeKind::Error)
                    Error("DBC2042", "Condition must be numeric or boolean", Span);
            }

            void CheckSupport(const SymbolDescriptor& Descriptor, SourceSpan Span)
            {
                if (Options_.RejectUnsupported && SupportFor(Descriptor, Source_.Profile) == SupportStatus::Unsupported)
                {
                    Error("DBC2043",
                          std::string(Descriptor.Kind == SymbolKind::Command ? "Command '" : "Function '") +
                              std::string(Descriptor.CanonicalName) + "' is explicitly unsupported",
                          Span);
                }
            }

            void CheckArity(const SymbolDescriptor& Descriptor, std::size_t Count, SourceSpan Span)
            {
                const std::optional<Arity> Expected = ArityFor(Descriptor, Source_.Profile);
                if (!Expected || Expected->Accepts(static_cast<int>(Count)))
                    return;
                Error("DBC2044",
                      std::string(Descriptor.CanonicalName) + " does not accept " + std::to_string(Count) +
                          " arguments in " + std::string(ProfileName(Source_.Profile)),
                      Span);
            }

            static Type TypeForBuiltin(BuiltinValueType Value)
            {
                switch (Value)
                {
                    case BuiltinValueType::Integer:
                        return {TypeKind::Integer, {}};
                    case BuiltinValueType::Real:
                    case BuiltinValueType::Numeric:
                    case BuiltinValueType::Any:
                        return {TypeKind::Real, {}};
                    case BuiltinValueType::String:
                        return {TypeKind::String, {}};
                    case BuiltinValueType::Void:
                        return {TypeKind::Void, {}};
                }
                return {TypeKind::Error, {}};
            }

            static bool AcceptsBuiltinType(BuiltinValueType Expected, const Type& Actual)
            {
                if (Actual.Kind == TypeKind::Error || Expected == BuiltinValueType::Any)
                    return true;
                switch (Expected)
                {
                    case BuiltinValueType::Numeric:
                        return Actual.IsNumeric();
                    case BuiltinValueType::Integer:
                        return Actual.Kind == TypeKind::Integer || Actual.Kind == TypeKind::Boolean;
                    case BuiltinValueType::Real:
                        return Actual.IsNumeric();
                    case BuiltinValueType::String:
                        return Actual.Kind == TypeKind::String;
                    case BuiltinValueType::Void:
                        return Actual.Kind == TypeKind::Void;
                    case BuiltinValueType::Any:
                        return true;
                }
                return false;
            }

            void ValidateOverload(const BuiltinOverload* Selected,
                                  const std::vector<std::unique_ptr<Expression>>& Arguments, SourceSpan Span)
            {
                if (!Selected)
                    return;
                for (std::size_t Index = 0; Index < Arguments.size() && Index < Selected->Parameters.size(); ++Index)
                {
                    const BuiltinParameter& Parameter = Selected->Parameters[Index];
                    if (!AcceptsBuiltinType(Parameter.ValueType, Arguments[Index]->ResolvedType))
                    {
                        Error("DBC2048",
                              "Argument " + std::to_string(Index + 1) + " of '" +
                                  std::string(Symbol(Selected->Symbol).CanonicalName) + "' must be " +
                                  BuiltinTypeName(Parameter.ValueType) + ", got " +
                                  TypeName(Arguments[Index]->ResolvedType),
                              Arguments[Index]->Span.LogicalPath.empty() ? Span : Arguments[Index]->Span);
                    }
                }
            }

            static std::string BuiltinTypeName(BuiltinValueType Value)
            {
                switch (Value)
                {
                    case BuiltinValueType::Any:
                        return "a value";
                    case BuiltinValueType::Numeric:
                        return "numeric";
                    case BuiltinValueType::Integer:
                        return "integer";
                    case BuiltinValueType::Real:
                        return "real";
                    case BuiltinValueType::String:
                        return "string";
                    case BuiltinValueType::Void:
                        return "void";
                }
                return "a value";
            }

            static std::string TypeName(const Type& Value)
            {
                switch (Value.Kind)
                {
                    case TypeKind::Error:
                        return "<error>";
                    case TypeKind::Unresolved:
                        return "<unresolved>";
                    case TypeKind::Void:
                        return "void";
                    case TypeKind::Boolean:
                        return "boolean";
                    case TypeKind::Integer:
                        return "integer";
                    case TypeKind::Real:
                        return "real";
                    case TypeKind::String:
                        return "string";
                    case TypeKind::UserDefined:
                        return Value.Name;
                }
                return "<unknown>";
            }

            static Type TypeFromSuffix(std::string_view Name)
            {
                if (!Name.empty() && Name.back() == '$')
                    return {TypeKind::String, {}};
                return {TypeKind::Real, {}};
            }

            void Error(std::string Code, std::string Message, SourceSpan Span)
            {
                AddError(Program_.Diagnostics, std::move(Code), std::move(Message), Span);
            }

            void Duplicate(std::string Code, std::string Message, SourceSpan Current, SourceSpan Previous)
            {
                Diagnostic Value{std::move(Code), DiagnosticSeverity::Error, std::move(Message), Current, {}};
                Value.Notes.push_back({"Previous declaration is here", Previous});
                Program_.Diagnostics.push_back(std::move(Value));
            }

            Program& Program_;
            const SourceDocument& Source_;
            FrontendOptions Options_{};
            std::unordered_map<std::string, TypeDeclaration*> Types_{};
            std::unordered_map<std::string, FunctionSymbol> Functions_{};
            std::unordered_map<std::string, SourceSpan> Labels_{};
            std::unordered_map<std::string, Type> Constants_{};
            std::unordered_map<std::string, SourceSpan> ConstantSpans_{};
            std::unordered_map<std::string, VariableSymbol> Globals_{};
            enum class InferenceState : std::uint8_t
            {
                Pending,
                InProgress,
                Done,
            };
            std::unordered_map<const FunctionDeclaration*, InferenceState> Inference_{};
            std::unordered_map<std::string, Type> DeclaredGlobals_{};
            std::unordered_map<std::string, Type> DeclaredArrays_{};
            std::unordered_map<std::string, VariableSymbol>* CurrentLocals_ = nullptr;
            FunctionDeclaration* CurrentFunction_ = nullptr;
            bool BindingConstant_ = false;
        };
    } // namespace

    void BindProgram(Program& Value, const FrontendOptions& Options)
    {
        Binder(Value, Options).Run();
    }
} // namespace dbc::language::detail
