#include "ASTScraper.hpp"

#include <iostream>

#include <clang/AST/RecordLayout.h>
#include <clang/AST/DeclTemplate.h>
#include "clang/AST/Decl.h"

#include "MetaCPP/Type.hpp"

namespace metacpp {
	ASTScraper::ASTScraper(clang::ASTContext* context, Storage* storage)
		: m_Context(context), m_Storage(storage)
	{
	}
		
	void ASTScraper::ScrapeDecl(const clang::Decl* decl)
	{
		/* Types */
		const clang::TypeDecl* typeDecl = clang::dyn_cast<clang::TypeDecl>(decl);
		if (typeDecl) {
			const clang::Type* type = typeDecl->getTypeForDecl();
			if (type)
				AddType(type);
			else {
				// Typedef
				auto typedefDecl = clang::dyn_cast<clang::TypedefDecl>(decl);
				if (typedefDecl) {
					type = typedefDecl->getUnderlyingType().getTypePtr();
					AddType(type);
				}
			}
		}

		/* Enums */
		// TODO
	}

	TypeID ASTScraper::AddType(const clang::Type* c_type)
	{
		// We can only scrape complete types
		if (!c_type || c_type->isIncompleteType())
			return 0;

		// If it its an elaborated type, we scrape the underlying type
		auto elType = clang::dyn_cast<clang::ElaboratedType>(c_type);
		if (elType)
			return AddType(elType->getNamedType().getTypePtr());

		// <fullName, name>
		auto& names = GetNameFromType(c_type);

		if (names.first.size() == 0)
			return 0;

		TypeID typeId = m_Storage->assignTypeID(names.first);
		
		// Have we already scraped this type?
		if (m_Storage->hasType(typeId))
			return typeId;
		
		Type* type = new Type(typeId, names.first, names.second);

		type->setSize(m_Context->getTypeSize(c_type) / 8);

		auto rDecl = c_type->getAsCXXRecordDecl();
		if (rDecl) {
			ScrapeRecord(rDecl, type);
		}
		else {
			type->setKind(TypeKind::PRIMITIVE);
		}

		m_Storage->addType(type);

		return typeId;
	}

	std::pair<std::string, std::string> ASTScraper::GetNameFromType(const clang::Type* type)
	{
		if (type->isBuiltinType()) {
			auto binType = type->getAs<clang::BuiltinType>();
			if (binType) {
				static clang::LangOptions lang_opts;
				static clang::PrintingPolicy printing_policy(lang_opts);

				std::string name = binType->getName(printing_policy);
				return std::make_pair(name, name);
			}
		}
		else {
			auto rDecl = type->getAsCXXRecordDecl();
			auto tsType = clang::dyn_cast<clang::TemplateSpecializationType>(type);
			
			if (tsType) {
				std::string specialized_args = "<";
				clang::ArrayRef<clang::TemplateArgument> args = tsType->template_arguments();
				for (const clang::TemplateArgument& arg : args) {
					TypeID arg_hash = AddType(arg.getAsType().getTypePtr());
					if(arg_hash)
						specialized_args += m_Storage->getType(arg_hash)->getFullName() + ",";
				}
				if(tsType->getNumArgs() > 0)
					specialized_args.pop_back();
				specialized_args += ">";

				return std::make_pair(rDecl->getQualifiedNameAsString() + specialized_args, rDecl->getNameAsString() + specialized_args);
			}
			else if (rDecl) {
				if (rDecl->hasDefinition() && !rDecl->isImplicit() && !rDecl->isDependentType())
					return std::make_pair(rDecl->getQualifiedNameAsString(), rDecl->getNameAsString());
			}
		}

		return std::make_pair("", "");
	}

	void ASTScraper::ScrapeRecord(const clang::CXXRecordDecl* rDecl, Type* type)
	{
		type->setKind(rDecl->isStruct() ? TypeKind::STRUCT : TypeKind::CLASS);

		/* Fields */
		for (auto it = rDecl->field_begin(); it != rDecl->field_end(); it++) {
			clang::FieldDecl* fieldDecl = *it;
			if (fieldDecl) {
				FieldID fieldId = ScrapeField(fieldDecl, type);
				if(fieldId != 0)
					type->addField(fieldId);
			}
		}
	}

	FieldID ASTScraper::ScrapeField(const clang::FieldDecl* fDecl, const Type* owner)
	{
		clang::QualType qualType = fDecl->getType();
		const clang::Type* c_type = qualType.getTypePtr();

		TypeID typeId = AddType(c_type);
		if (!typeId)
			return 0; // NULL

		std::string name = fDecl->getNameAsString();
		std::string fullName = owner->getFullName() + "::" + name;
		FieldID fieldId = m_Storage->assignFieldID(fullName);

		Field* field = new Field(fieldId, typeId, owner->getID(), fullName, name);

		unsigned int offset_bytes = m_Context->getFieldOffset(fDecl) / 8;
		field->setOffset(offset_bytes);

		m_Storage->addField(field);

		return fieldId;
	}
}
