// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements annotation matching and propagation logic for the CJMP (Cangjie Multi-Platform) feature.
 * It ensures that annotations between common declarations and platform-specific declarations are properly synchronized.
 *
 * Key Components:
 * - MatchCJMPDeclAnnotations(): Validates that common and platform declarations have matching annotations
 * - PropagateCJMPDeclAnnotations(): Propagates special annotations from common to platform declarations
 * - CheckNotAllowedAnnotations(): Validates that unsupported annotations are not used on common/platform declarations
 *
 * The implementation maintains consistency between common and platform-specific code annotations in the
 * multi-platform compilation model, ensuring proper annotation inheritance and synchronization.
 */
#include "MPTypeCheckerImpl.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Position.h"
#include "cangjie/Parse/Parser.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/SafePointer.h"
#include <algorithm>
#include <unordered_set>

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

namespace {
// Annotations not serialized in AST are matched via their corresponding attributes
// Annotations with parameters MUST be serialized for common/platform matching
const std::unordered_map<AnnotationKind, Attribute> NonSerializedAnnotations = {
    {AnnotationKind::C, Attribute::C},
    {AnnotationKind::JAVA_MIRROR, Attribute::JAVA_MIRROR},
    {AnnotationKind::JAVA_HAS_DEFAULT, Attribute::JAVA_HAS_DEFAULT},
    {AnnotationKind::OBJ_C_MIRROR, Attribute::OBJ_C_MIRROR},
    {AnnotationKind::OBJ_C_INIT, Attribute::OBJ_C_INIT},
    {AnnotationKind::OBJ_C_OPTIONAL, Attribute::OBJ_C_OPTIONAL},
};

// Annotations not supported on common/platform (neither serialized nor have attributes)
const std::unordered_set<AnnotationKind> UnsupportedAnnotations = {AnnotationKind::JAVA, AnnotationKind::CALLING_CONV,
    AnnotationKind::FOREIGN_GETTER_NAME, AnnotationKind::FOREIGN_SETTER_NAME, AnnotationKind::CONSTSAFE,
    AnnotationKind::ENSURE_PREPARED_TO_MOCK, AnnotationKind::NON_PRODUCT, AnnotationKind::UNKNOWN};

bool PostCheckDeprecatedAnnotation(const AST::Decl& platform, DiagnosticEngine& diag)
{
    auto platformDeprecation = FindFirstAnnotation(platform, AnnotationKind::DEPRECATED);

    if (platformDeprecation && !platformDeprecation->TestAttr(Attribute::COMPILER_ADD)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_has_deprecated_annotation, *platformDeprecation,
                "Deprecated", DeclKindToString(platform), platform.identifier.Val())
            .AddNote("platform declarations implicitly inherit deprecation from the common declarations");
        return false;
    }

    return true;
}

bool FuncArgEquals(const AST::FuncArg& a, const AST::FuncArg& b)
{
    if (a.name != b.name) {
        return false;
    }

    if (a.expr->astKind != ASTKind::LIT_CONST_EXPR || b.expr->astKind != ASTKind::LIT_CONST_EXPR) {
        // const expressions passing to annotations are not supported
        return false;
    }

    auto aLit = StaticAs<ASTKind::LIT_CONST_EXPR>(a.expr.get());
    auto bLit = StaticAs<ASTKind::LIT_CONST_EXPR>(b.expr.get());

    if (aLit->kind != bLit->kind) {
        return false;
    }

    return aLit->ToString() == bLit->ToString();
}

bool AnnotationEquals(const AST::Annotation& a, const AST::Annotation& b)
{
    if (a.kind != b.kind) {
        return false;
    }
    if (a.kind == AnnotationKind::CUSTOM && a.identifier.Val() != b.identifier.Val()) {
        return false;
    }
    if (a.args.size() != b.args.size()) {
        return false;
    }

    // named parameters are not supported but we check parameter names just in case
    // inside of FuncArgEquals, will only work if named arguments are at the same positions
    for (size_t i = 0; i < a.args.size(); ++i) {
        if (!FuncArgEquals(*a.args[i], *b.args[i])) {
            return false;
        }
    }

    return true;
}

Ptr<const AST::Annotation> FindAnnotation(const AST::Decl& decl, const AST::Annotation& sample,
    const std::unordered_set<Ptr<const AST::Annotation>>& excludes)
{
    auto found = std::find_if(decl.annotations.begin(), decl.annotations.end(), [&sample, &excludes](const auto& anno) {
        return excludes.count(anno.get()) == 0 && AnnotationEquals(sample, *anno.get());
    });
    if (found != decl.annotations.end()) {
        return (*found).get();
    }
    return nullptr;
}

std::string FormatAnnotationMismatchNote(const AST::Annotation& anno)
{
    std::ostringstream result;
    result << "The mismatched annotation is @" << anno.identifier.Val();
    if (!anno.args.empty()) {
        result << "(";
        int index = 0;
        for (auto& arg : anno.args) {
            if (index > 0) {
                result << ", ";
            }
            index++;
            result << arg->ToString();
        }
        result << ")";
    }
    return result.str();
}

std::string FormatAnnotationMismatchNote(AnnotationKind kind)
{
    std::ostringstream noteMessage;
    noteMessage << "The mismatched annotation is @" << AnnotationKindToString(kind);
    return noteMessage.str();
}

void ReportAnnotationMismatch(const AST::Annotation& anno, const AST::Decl& decl, DiagnosticEngine& diag)
{
    if (decl.TestAttr(Attribute::PLATFORM)) {
        // The declaration is platform-specific but the mismatching annotation belongs to the common declaration.
        // We report the error at the platform declaration location since it has source positions.
        // The common annotation is deserialized from metadata and lacks source position information,
        // so we cannot provide a hint pointing to it. Instead, we generate a descriptive note
        // explaining which annotation is mismatched.
        auto note = FormatAnnotationMismatchNote(anno);
        // note: decl is platform decl
        diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_has_different_annotation, decl, DeclKindToString(decl))
            .AddNote(note);
    } else {
        // note: decl is common here
        auto msg = diag.DiagnoseRefactor(
            DiagKindRefactor::sema_platform_has_different_annotation, anno, DeclKindToString(decl));
        msg.AddHint(decl, "The corresponding common declaration");
        msg.AddNote("Either remove the platform annotation or add it to the common declaration");
    }
}

bool PostCheckNonSerializedAnnotations(const AST::Decl& common, const AST::Decl& platform, DiagnosticEngine& diag)
{
    bool result = true;

    // Iterate over all non-serialized annotations and check attribute consistency
    for (const auto& [annoKind, attr] : NonSerializedAnnotations) {
        if (common.TestAttr(attr) != platform.TestAttr(attr)) {
            // Find the platform annotation for this type to use in diagnostic
            auto platformAnno = FindFirstAnnotation(platform, annoKind);

            if (platformAnno) {
                // we have a platform annotation but common doesn't have it
                ReportAnnotationMismatch(*platformAnno, platform, diag);
            } else {
                // there is an annotation in common but platform doesn't have it
                // for non-serialized annotations from common we don't have an instance
                // so we cannot use ReportAnnotationMismatch function
                diag.DiagnoseRefactor(
                        DiagKindRefactor::sema_platform_has_different_annotation, platform, DeclKindToString(platform))
                    .AddNote(FormatAnnotationMismatchNote(annoKind));
            }

            result = false;
        }
    }

    return result;
}

bool IsSpecialHandledAnnotation(const Annotation& anno)
{
    return anno.kind == AnnotationKind::DEPRECATED || anno.kind == AnnotationKind::ATTRIBUTE ||
        NonSerializedAnnotations.find(anno.kind) != NonSerializedAnnotations.end() ||
        UnsupportedAnnotations.find(anno.kind) != UnsupportedAnnotations.end();
}

/**
 * @brief Matches annotations from source declaration to target declaration.
 *
 * This helper function performs the core annotation matching logic for one direction
 * (either common→platform or platform→common). It iterates through all annotations
 * on the source declaration and attempts to find matching annotations on the target
 * declaration, reporting mismatches when found.
 *
 * @param source The declaration containing annotations to match
 * @param target The declaration to find matching annotations in
 * @param matchedSource Set to track annotations already matched on source side
 * @param matchedTarget Set to track annotations already matched on target side
 * @param diag Diagnostic engine for reporting mismatches
 * @return bool True if all source annotations matched successfully, false otherwise
 */
bool MatchAnnotationsFromSource(const AST::Decl& source, const AST::Decl& target,
    std::unordered_set<Ptr<const AST::Annotation>>& matchedSource,
    std::unordered_set<Ptr<const AST::Annotation>>& matchedTarget, DiagnosticEngine& diag)
{
    bool result = true;

    for (auto& anno : source.annotations) {
        if (IsSpecialHandledAnnotation(*anno) || matchedSource.count(anno.get())) {
            continue;
        }
        auto targetMatch = FindAnnotation(target, *anno.get(), matchedTarget);
        if (targetMatch) {
            matchedSource.insert(anno.get());
            matchedTarget.insert(targetMatch);
        } else {
            ReportAnnotationMismatch(*anno.get(), target, diag);
            result = false;
        }
    }

    return result;
}
} // namespace

/**
 * @brief Validates that common and platform declarations have matching annotations.
 *
 * This function implements the CJMP rule that common and platform declarations must have
 * matching annotations (except for @Deprecated which is handled separately). It performs
 * a bidirectional check to ensure all annotations on both declarations have corresponding
 * matches on the other side.
 *
 * The algorithm:
 * 1. Checks deprecation annotation rules first via PostCheckDeprecatedAnnotation
 * 2. For each annotation on the common declaration, finds a matching annotation on the platform declaration
 * 3. For each annotation on the platform declaration, finds a matching annotation on the common declaration
 * 4. Reports mismatches with detailed diagnostic messages
 *
 * @note This function handles the case when the same annotation is applied multiple times
 *       to the same declaration. Each occurrence is matched separately to ensure the exact
 *       count of annotations matches between common and platform declarations.
 * @note The function matches annotation arguments (position-based) but doesn't handle named parameters.
 *       Only positional arguments are supported for annotation matching.
 *
 * @param common The common declaration to match annotations from
 * @param platform The platform declaration to match annotations against
 * @return bool True if all annotations match successfully, false otherwise
 */
bool MPTypeCheckerImpl::MatchCJMPDeclAnnotations(const AST::Decl& common, AST::Decl& platform) const
{
    bool result = PostCheckDeprecatedAnnotation(platform, diag);
    result &= PostCheckNonSerializedAnnotations(common, platform, diag);

    // Two sets are required to track which annotations have already been matched to handle
    // duplicate annotations correctly. They ensure each annotation on one side finds exactly
    // one matching annotation on the other side, preventing multiple annotations from
    // matching the same target.
    std::unordered_set<Ptr<const AST::Annotation>> matchedCommonAnnotations;
    std::unordered_set<Ptr<const AST::Annotation>> matchedPlatformAnnotations;

    // Match annotations from common declaration to platform declaration
    result &= MatchAnnotationsFromSource(common, platform, matchedCommonAnnotations, matchedPlatformAnnotations, diag);

    // Match annotations from platform declaration to common declaration
    result &= MatchAnnotationsFromSource(platform, common, matchedPlatformAnnotations, matchedCommonAnnotations, diag);

    return result;
}

namespace {
bool IsCommonOrPlatform(const Node& node)
{
    return node.TestAnyAttr(Attribute::COMMON, Attribute::PLATFORM);
}

bool IsUnsupported(const Annotation& anno)
{
    return UnsupportedAnnotations.count(anno.kind);
}
} // namespace

void MPTypeCheckerImpl::CheckNotAllowedAnnotations(AST::Package& pkg)
{
    std::function<VisitAction(Ptr<Node>)> visitor = [this](const Ptr<Node>& node) {
        if (node->IsDecl() && IsCommonOrPlatform(*node) && !node->TestAttr(Attribute::FROM_COMMON_PART)) {
            for (auto& anno : StaticCast<Decl>(node)->annotations) {
                if (IsUnsupported(*anno)) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_common_platform_annotation_not_allowed, *anno,
                            AnnotationKindToString(anno->kind))
                        .AddNote("This annotation kind is not supported on common/platform");
                }
            }
        }
        if (node->astKind == ASTKind::FUNC_BODY) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&pkg, visitor);
    walker.Walk();
}

namespace {
/**
 * @brief Propagates deprecation annotations from common declarations to platform declarations.
 *
 * This function implements the CJMP rule that platform declarations implicitly inherit
 * deprecation annotations from their corresponding common declarations. It only copies
 * the @Deprecated annotation if the common declaration has one and the platform declaration
 * doesn't already have one.
 *
 * @param common The common declaration that may contain deprecation annotation
 * @param platform The platform declaration to receive the deprecation annotation (if applicable)
 */
void PropagateDeprecatedAnnotations(const AST::Decl& common, AST::Decl& platform)
{
    auto commonDeprecation = FindFirstAnnotation(common, AnnotationKind::DEPRECATED);
    auto platformDeprecation = FindFirstAnnotation(platform, AnnotationKind::DEPRECATED);

    if (!commonDeprecation || platformDeprecation) {
        return;
    }

    platform.annotations.emplace_back(ASTCloner::Clone(commonDeprecation));
}

/**
 * @brief Propagates @Attribute annotations from common declarations to platform declarations.
 *
 * @Attribute annotations are handled via propagation rather than matching since they
 * don't have corresponding attributes for comparison. This function copies all @Attribute
 * annotations from the common declaration to the platform declaration.
 *
 * @note Only @Attribute annotations with "TEST_REGISTER" are serialized for now.
 *       The other @Attribute annotations are not serialized and will be silently ignored.
 *       In order to make it work, the proper serialization support needs to be implemented.
 *
 * @param common The common declaration containing @Attribute annotations
 * @param platform The platform declaration to receive the @Attribute annotations
 */
void PropagateAttributeAnnotations(const AST::Decl& common, AST::Decl& platform)
{
    for (auto& annotation : common.annotations) {
        if (annotation->kind == AnnotationKind::ATTRIBUTE) {
            platform.annotations.emplace_back(ASTCloner::Clone(annotation.get()));
        }
    }
}
} // namespace

/**
 * @brief Propagates special annotations from common declarations to platform declarations.
 *
 * This function handles annotations that cannot be matched directly between common and platform
 * declarations and must be propagated instead. Currently handles @Deprecated and @Attribute
 * annotations which have special propagation rules in CJMP.
 *
 * @param common The common declaration containing annotations to propagate
 * @param platform The platform declaration to receive the propagated annotations
 */
void MPTypeCheckerImpl::PropagateCJMPDeclAnnotations(const AST::Decl& common, AST::Decl& platform) const
{
    PropagateDeprecatedAnnotations(common, platform);
    PropagateAttributeAnnotations(common, platform);
}
