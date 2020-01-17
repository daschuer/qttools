/****************************************************************************
**
** Copyright (C) 2019 Thibaut Cuvelier
** Contact: https://www.qt.io/licensing/
**
** This file is part of the tools applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <cctype>
#include <qlist.h>
#include <qiterator.h>
#include <qtextcodec.h>
#include <quuid.h>
#include <qurl.h>
#include <qmap.h>
#include <QtCore/qversionnumber.h>

#include "codemarker.h"
#include "config.h"
#include "generator.h"
#include "docbookgenerator.h"
#include "node.h"
#include "quoter.h"
#include "qdocdatabase.h"
#include "separator.h"

QT_BEGIN_NAMESPACE

static const char dbNamespace[] = "http://docbook.org/ns/docbook";
static const char xlinkNamespace[] = "http://www.w3.org/1999/xlink";

static void newLine(QXmlStreamWriter &writer)
{
    writer.writeCharacters("\n");
}

static void startSectionBegin(QXmlStreamWriter &writer)
{
    writer.writeStartElement(dbNamespace, "section");
    newLine(writer);
    writer.writeStartElement(dbNamespace, "title");
}

static void startSectionBegin(QXmlStreamWriter &writer, const QString &id)
{
    writer.writeStartElement(dbNamespace, "section");
    writer.writeAttribute("xml:id", id);
    newLine(writer);
    writer.writeStartElement(dbNamespace, "title");
}

static void startSectionEnd(QXmlStreamWriter &writer)
{
    writer.writeEndElement(); // title
    newLine(writer);
}

static void startSection(QXmlStreamWriter &writer, const QString &id, const QString &title)
{
    startSectionBegin(writer, id);
    writer.writeCharacters(title);
    startSectionEnd(writer);
}

static void endSection(QXmlStreamWriter &writer)
{
    writer.writeEndElement(); // section
    newLine(writer);
}

static void writeAnchor(QXmlStreamWriter &writer, QString id)
{
    writer.writeEmptyElement(dbNamespace, "anchor");
    writer.writeAttribute("xml:id", id);
    newLine(writer);
}

/*!
  Initializes the DocBook output generator's data structures
  from the configuration (Config).
 */
void DocBookGenerator::initializeGenerator()
{
    // Excerpts from HtmlGenerator::initializeGenerator.
    Generator::initializeGenerator();
    Config &config = Config::instance();

    project = config.getString(CONFIG_PROJECT);

    projectDescription = config.getString(CONFIG_DESCRIPTION);
    if (projectDescription.isEmpty() && !project.isEmpty())
        projectDescription = project + QLatin1String(" Reference Documentation");

    naturalLanguage = config.getString(CONFIG_NATURALLANGUAGE);
    if (naturalLanguage.isEmpty())
        naturalLanguage = QLatin1String("en");

    buildversion = config.getString(CONFIG_BUILDVERSION);
}

QString DocBookGenerator::format()
{
    return QStringLiteral("DocBook");
}

/*!
  Returns "xml" for this subclass of Generator.
 */
QString DocBookGenerator::fileExtension() const
{
    return QStringLiteral("xml");
}

/*!
  Generate the documentation for \a relative. i.e. \a relative
  is the node that represents the entity where a qdoc comment
  was found, and \a text represents the qdoc comment.
 */
bool DocBookGenerator::generateText(QXmlStreamWriter &writer, const Text &text,
                                    const Node *relative)
{
    // From Generator::generateText.
    if (!text.firstAtom())
        return false;

    int numAtoms = 0;
    initializeTextOutput();
    generateAtomList(writer, text.firstAtom(), relative, true, numAtoms);
    closeTextSections(writer);
    return true;
}

/*!
  Generate the text for \a atom relatively to \a relative.
  \a generate indicates if output to \a writer is expected.
  The number of generated atoms is returned in the argument
  \a numAtoms. The returned value is the first atom that was not
  generated.
 */
const Atom *DocBookGenerator::generateAtomList(QXmlStreamWriter &writer, const Atom *atom,
                                               const Node *relative, bool generate, int &numAtoms)
{
    // From Generator::generateAtomList.
    while (atom) {
        switch (atom->type()) {
        case Atom::FormatIf: {
            int numAtoms0 = numAtoms;
            atom = generateAtomList(writer, atom->next(), relative, generate, numAtoms);
            if (!atom)
                return nullptr;

            if (atom->type() == Atom::FormatElse) {
                ++numAtoms;
                atom = generateAtomList(writer, atom->next(), relative, false, numAtoms);
                if (!atom)
                    return nullptr;
            }

            if (atom->type() == Atom::FormatEndif) {
                if (generate && numAtoms0 == numAtoms) {
                    relative->location().warning(
                            tr("Output format %1 not handled %2").arg(format()).arg(outFileName()));
                    Atom unhandledFormatAtom(Atom::UnhandledFormat, format());
                    generateAtomList(writer, &unhandledFormatAtom, relative, generate, numAtoms);
                }
                atom = atom->next();
            }
        } break;
        case Atom::FormatElse:
        case Atom::FormatEndif:
            return atom;
        default:
            int n = 1;
            if (generate) {
                n += generateAtom(writer, atom, relative);
                numAtoms += n;
            }
            while (n-- > 0)
                atom = atom->next();
        }
    }
    return nullptr;
}

/*!
  Generate DocBook from an instance of Atom.
 */
int DocBookGenerator::generateAtom(QXmlStreamWriter &writer, const Atom *atom, const Node *relative)
{
    // From HtmlGenerator::generateAtom, without warning generation.
    int idx, skipAhead = 0;
    static bool inPara = false;

    switch (atom->type()) {
    case Atom::AutoLink:
    case Atom::NavAutoLink:
        if (!inLink && !inContents_ && !inSectionHeading_) {
            const Node *node = nullptr;
            QString link = getAutoLink(atom, relative, &node);
            if (!link.isEmpty() && node && node->status() == Node::Obsolete
                && relative->parent() != node && !relative->isObsolete()) {
                link.clear();
            }
            if (link.isEmpty()) {
                writer.writeCharacters(atom->string());
            } else {
                beginLink(writer, link, node, relative);
                generateLink(writer, atom);
                endLink(writer);
            }
        } else {
            writer.writeCharacters(atom->string());
        }
        break;
    case Atom::BaseName:
        break;
    case Atom::BriefLeft:
        if (!hasBrief(relative)) {
            skipAhead = skipAtoms(atom, Atom::BriefRight);
            break;
        }
        writer.writeStartElement(dbNamespace, "para");
        rewritePropertyBrief(atom, relative);
        break;
    case Atom::BriefRight:
        if (hasBrief(relative)) {
            writer.writeEndElement(); // para
            newLine(writer);
        }
        break;
    case Atom::C:
        // This may at one time have been used to mark up C++ code but it is
        // now widely used to write teletype text. As a result, text marked
        // with the \c command is not passed to a code marker.
        writer.writeTextElement(dbNamespace, "code", plainCode(atom->string()));
        break;
    case Atom::CaptionLeft:
        writer.writeStartElement(dbNamespace, "title");
        break;
    case Atom::CaptionRight:
        endLink(writer);
        writer.writeEndElement(); // title
        newLine(writer);
        break;
    case Atom::Qml:
        writer.writeStartElement(dbNamespace, "programlisting");
        writer.writeAttribute("language", "qml");
        writer.writeCharacters(atom->string());
        writer.writeEndElement(); // programlisting
        newLine(writer);
        break;
    case Atom::JavaScript:
        writer.writeStartElement(dbNamespace, "programlisting");
        writer.writeAttribute("language", "js");
        writer.writeCharacters(atom->string());
        writer.writeEndElement(); // programlisting
        newLine(writer);
        break;
    case Atom::CodeNew:
        writer.writeTextElement(dbNamespace, "para", "you can rewrite it as");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "programlisting");
        writer.writeAttribute("language", "cpp");
        writer.writeAttribute("role", "new");
        writer.writeCharacters(atom->string());
        writer.writeEndElement(); // programlisting
        newLine(writer);
        break;
    case Atom::Code:
        writer.writeStartElement(dbNamespace, "programlisting");
        writer.writeAttribute("language", "cpp");
        writer.writeCharacters(atom->string());
        writer.writeEndElement(); // programlisting
        newLine(writer);
        break;
    case Atom::CodeOld:
        writer.writeTextElement(dbNamespace, "para", "For example, if you have code like");
        newLine(writer);
        Q_FALLTHROUGH();
    case Atom::CodeBad:
        writer.writeStartElement(dbNamespace, "programlisting");
        writer.writeAttribute("language", "cpp");
        writer.writeAttribute("role", "bad");
        writer.writeCharacters(atom->string());
        writer.writeEndElement(); // programlisting
        newLine(writer);
        break;
    case Atom::DivLeft:
    case Atom::DivRight:
        break;
    case Atom::FootnoteLeft:
        writer.writeStartElement(dbNamespace, "footnote");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "para");
        break;
    case Atom::FootnoteRight:
        writer.writeEndElement(); // para
        newLine(writer);
        writer.writeEndElement(); // footnote
        break;
    case Atom::FormatElse:
    case Atom::FormatEndif:
    case Atom::FormatIf:
        break;
    case Atom::FormattingLeft:
        if (atom->string() == ATOM_FORMATTING_BOLD) {
            writer.writeStartElement(dbNamespace, "emphasis");
            writer.writeAttribute("role", "bold");
        } else if (atom->string() == ATOM_FORMATTING_ITALIC) {
            writer.writeStartElement(dbNamespace, "emphasis");
        } else if (atom->string() == ATOM_FORMATTING_UNDERLINE) {
            writer.writeStartElement(dbNamespace, "emphasis");
            writer.writeAttribute("role", "underline");
        } else if (atom->string() == ATOM_FORMATTING_SUBSCRIPT) {
            writer.writeStartElement(dbNamespace, "sub");
        } else if (atom->string() == ATOM_FORMATTING_SUPERSCRIPT) {
            writer.writeStartElement(dbNamespace, "sup");
        } else if (atom->string() == ATOM_FORMATTING_TELETYPE || atom->string() == ATOM_FORMATTING_PARAMETER) {
            writer.writeStartElement(dbNamespace, "code");

            if (atom->string() == ATOM_FORMATTING_PARAMETER)
                writer.writeAttribute("role", "parameter");
        }
        break;
    case Atom::FormattingRight:
        if (atom->string() == ATOM_FORMATTING_BOLD || atom->string() == ATOM_FORMATTING_ITALIC
            || atom->string() == ATOM_FORMATTING_UNDERLINE
            || atom->string() == ATOM_FORMATTING_SUBSCRIPT
            || atom->string() == ATOM_FORMATTING_SUPERSCRIPT
            || atom->string() == ATOM_FORMATTING_TELETYPE
            || atom->string() == ATOM_FORMATTING_PARAMETER) {
            writer.writeEndElement();
        }
        if (atom->string() == ATOM_FORMATTING_LINK)
            endLink(writer);
        break;
    case Atom::AnnotatedList:
        if (const CollectionNode *cn = qdb_->getCollectionNode(atom->string(), Node::Group))
            generateList(writer, cn, atom->string());
        break;
    case Atom::GeneratedList:
        if (atom->string() == QLatin1String("annotatedclasses")
            || atom->string() == QLatin1String("attributions")
            || atom->string() == QLatin1String("namespaces")) {
            const NodeMultiMap things = atom->string() == QLatin1String("annotatedclasses")
                    ? qdb_->getCppClasses()
                    : atom->string() == QLatin1String("attributions") ? qdb_->getAttributions()
                                                                      : qdb_->getNamespaces();
            generateAnnotatedList(writer, relative, things, atom->string());
        } else if (atom->string() == QLatin1String("annotatedexamples")
                   || atom->string() == QLatin1String("annotatedattributions")) {
            const NodeMultiMap things = atom->string() == QLatin1String("annotatedexamples")
                    ? qdb_->getAttributions()
                    : qdb_->getExamples();
            generateAnnotatedLists(writer, relative, things, atom->string());
        } else if (atom->string() == QLatin1String("classes")
                   || atom->string() == QLatin1String("qmlbasictypes")
                   || atom->string() == QLatin1String("qmltypes")) {
            const NodeMultiMap things = atom->string() == QLatin1String("classes")
                    ? qdb_->getCppClasses()
                    : atom->string() == QLatin1String("qmlbasictypes") ? qdb_->getQmlBasicTypes()
                                                                       : qdb_->getQmlTypes();
            generateCompactList(writer, Generic, relative, things, QString(), atom->string());
        } else if (atom->string().contains("classes ")) {
            QString rootName = atom->string().mid(atom->string().indexOf("classes") + 7).trimmed();
            generateCompactList(writer, Generic, relative, qdb_->getCppClasses(), rootName,
                                atom->string());
        } else if ((idx = atom->string().indexOf(QStringLiteral("bymodule"))) != -1) {
            QString moduleName = atom->string().mid(idx + 8).trimmed();
            Node::NodeType type = typeFromString(atom);
            QDocDatabase *qdb = QDocDatabase::qdocDB();
            if (const CollectionNode *cn = qdb->getCollectionNode(moduleName, type)) {
                if (type == Node::Module) {
                    NodeMap m;
                    cn->getMemberClasses(m);
                    if (!m.isEmpty())
                        generateAnnotatedList(writer, relative, m, atom->string());
                } else {
                    generateAnnotatedList(writer, relative, cn->members(), atom->string());
                }
            }
        } else if (atom->string().startsWith("examplefiles")
                   || atom->string().startsWith("exampleimages")) {
            if (relative->isExample())
                qDebug() << "GENERATE FILE LIST CALLED" << relative->name() << atom->string();
        } else if (atom->string() == QLatin1String("classhierarchy")) {
            generateClassHierarchy(writer, relative, qdb_->getCppClasses());
        } else if (atom->string().startsWith("obsolete")) {
            ListType type = atom->string().endsWith("members") ? Obsolete : Generic;
            QString prefix = atom->string().contains("cpp") ? QStringLiteral("Q") : QString();
            const NodeMultiMap &things = atom->string() == QLatin1String("obsoleteclasses")
                    ? qdb_->getObsoleteClasses()
                    : atom->string() == QLatin1String("obsoleteqmltypes")
                            ? qdb_->getObsoleteQmlTypes()
                            : atom->string() == QLatin1String("obsoletecppmembers")
                                    ? qdb_->getClassesWithObsoleteMembers()
                                    : qdb_->getQmlTypesWithObsoleteMembers();
            generateCompactList(writer, type, relative, things, prefix, atom->string());
        } else if (atom->string() == QLatin1String("functionindex")) {
            generateFunctionIndex(writer, relative);
        } else if (atom->string() == QLatin1String("legalese")) {
            generateLegaleseList(writer, relative);
        } else if (atom->string() == QLatin1String("overviews")
                   || atom->string() == QLatin1String("cpp-modules")
                   || atom->string() == QLatin1String("qml-modules")
                   || atom->string() == QLatin1String("related")) {
            generateList(writer, relative, atom->string());
        }
        break;
    case Atom::SinceList:
        // Table of contents, should automatically be generated by the DocBook processor.
        break;
    case Atom::LineBreak:
    case Atom::BR:
    case Atom::HR:
        // Not supported in DocBook.
        break;
    case Atom::Image: // mediaobject
    case Atom::InlineImage: { // inlinemediaobject
        QString tag = atom->type() == Atom::Image ? "mediaobject" : "inlinemediaobject";
        writer.writeStartElement(dbNamespace, tag);
        newLine(writer);

        QString fileName = imageFileName(relative, atom->string());
        if (fileName.isEmpty()) {
            writer.writeStartElement(dbNamespace, "textobject");
            newLine(writer);
            writer.writeStartElement(dbNamespace, "para");
            writer.writeTextElement(dbNamespace, "emphasis",
                                    "[Missing image " + atom->string() + "]");
            writer.writeEndElement(); // para
            newLine(writer);
            writer.writeEndElement(); // textobject
            newLine(writer);
        } else {
            if (atom->next() && !atom->next()->string().isEmpty())
                writer.writeTextElement(dbNamespace, "alt", atom->next()->string());

            writer.writeStartElement(dbNamespace, "imageobject");
            newLine(writer);
            writer.writeEmptyElement(dbNamespace, "imagedata");
            writer.writeAttribute("fileref", fileName);
            newLine(writer);
            writer.writeEndElement(); // imageobject
            newLine(writer);

            setImageFileName(relative, fileName);
        }

        writer.writeEndElement(); // [inline]mediaobject
        if (atom->type() == Atom::Image)
            newLine(writer);
    } break;
    case Atom::ImageText:
        break;
    case Atom::ImportantLeft:
    case Atom::NoteLeft: {
        QString tag = atom->type() == Atom::ImportantLeft ? "important" : "note";
        writer.writeStartElement(dbNamespace, tag);
        newLine(writer);
        writer.writeStartElement(dbNamespace, "para");
    } break;
    case Atom::ImportantRight:
    case Atom::NoteRight:
        writer.writeEndElement(); // para
        newLine(writer);
        writer.writeEndElement(); // note/important
        newLine(writer);
        break;
    case Atom::LegaleseLeft:
    case Atom::LegaleseRight:
        break;
    case Atom::Link:
    case Atom::NavLink: {
        const Node *node = nullptr;
        QString link = getLink(atom, relative, &node);
        beginLink(writer, link, node, relative); // Ended at Atom::FormattingRight
        skipAhead = 1;
    } break;
    case Atom::LinkNode: {
        const Node *node = CodeMarker::nodeForString(atom->string());
        beginLink(writer, linkForNode(node, relative), node, relative);
        skipAhead = 1;
    } break;
    case Atom::ListLeft:
        if (inPara) {
            writer.writeEndElement(); // para
            newLine(writer);
            inPara = false;
        }
        if (atom->string() == ATOM_LIST_BULLET) {
            writer.writeStartElement(dbNamespace, "itemizedlist");
            newLine(writer);
        } else if (atom->string() == ATOM_LIST_TAG) {
            writer.writeStartElement(dbNamespace, "variablelist");
            newLine(writer);
        } else if (atom->string() == ATOM_LIST_VALUE) {
            writer.writeStartElement(dbNamespace, "informaltable");
            newLine(writer);
            writer.writeStartElement(dbNamespace, "thead");
            newLine(writer);
            writer.writeStartElement(dbNamespace, "tr");
            newLine(writer);
            writer.writeTextElement(dbNamespace, "th", "Constant");
            newLine(writer);

            threeColumnEnumValueTable_ = isThreeColumnEnumValueTable(atom);
            if (threeColumnEnumValueTable_ && relative->nodeType() == Node::Enum) {
                // If not in \enum topic, skip the value column
                writer.writeTextElement(dbNamespace, "th", "Value");
                newLine(writer);
            }

            writer.writeTextElement(dbNamespace, "th", "Description");
            newLine(writer);

            writer.writeEndElement(); // tr
            newLine(writer);
            writer.writeEndElement(); // thead
            newLine(writer);
        } else {
            writer.writeStartElement(dbNamespace, "orderedlist");

            if (atom->next() != nullptr && atom->next()->string().toInt() > 1)
                writer.writeAttribute("startingnumber", atom->next()->string());

            if (atom->string() == ATOM_LIST_UPPERALPHA)
                writer.writeAttribute("numeration", "upperalpha");
            else if (atom->string() == ATOM_LIST_LOWERALPHA)
                writer.writeAttribute("numeration", "loweralpha");
            else if (atom->string() == ATOM_LIST_UPPERROMAN)
                writer.writeAttribute("numeration", "upperroman");
            else if (atom->string() == ATOM_LIST_LOWERROMAN)
                writer.writeAttribute("numeration", "lowerroman");
            else // (atom->string() == ATOM_LIST_NUMERIC)
                writer.writeAttribute("numeration", "arabic");

            newLine(writer);
        }
        break;
    case Atom::ListItemNumber:
        break;
    case Atom::ListTagLeft:
        if (atom->string() == ATOM_LIST_TAG) {
            writer.writeStartElement(dbNamespace, "varlistentry");
            newLine(writer);
            writer.writeStartElement(dbNamespace, "item");
        } else { // (atom->string() == ATOM_LIST_VALUE)
            QPair<QString, int> pair = getAtomListValue(atom);
            skipAhead = pair.second;

            writer.writeStartElement(dbNamespace, "tr");
            newLine(writer);
            writer.writeStartElement(dbNamespace, "td");
            newLine(writer);
            writer.writeStartElement(dbNamespace, "para");
            generateEnumValue(writer, pair.first, relative);
            writer.writeEndElement(); // para
            newLine(writer);
            writer.writeEndElement(); // td
            newLine(writer);

            if (relative->nodeType() == Node::Enum) {
                const auto enume = static_cast<const EnumNode *>(relative);
                QString itemValue = enume->itemValue(atom->next()->string());

                writer.writeStartElement(dbNamespace, "td");
                if (itemValue.isEmpty())
                    writer.writeCharacters("?");
                else
                    writer.writeTextElement(dbNamespace, "code", itemValue);
                writer.writeEndElement(); // td
                newLine(writer);
            }
        }
        break;
    case Atom::SinceTagRight:
    case Atom::ListTagRight:
        if (atom->string() == ATOM_LIST_TAG) {
            writer.writeEndElement(); // item
            newLine(writer);
        }
        break;
    case Atom::ListItemLeft:
        inListItemLineOpen = false;
        if (atom->string() == ATOM_LIST_TAG) {
            writer.writeStartElement(dbNamespace, "listitem");
            newLine(writer);
            writer.writeStartElement(dbNamespace, "para");
        } else if (atom->string() == ATOM_LIST_VALUE) {
            if (threeColumnEnumValueTable_) {
                if (matchAhead(atom, Atom::ListItemRight)) {
                    writer.writeEmptyElement(dbNamespace, "td");
                    newLine(writer);
                    inListItemLineOpen = false;
                } else {
                    writer.writeStartElement(dbNamespace, "td");
                    newLine(writer);
                    inListItemLineOpen = true;
                }
            }
        } else {
            writer.writeStartElement(dbNamespace, "listitem");
            newLine(writer);
        }
        // Don't skip a paragraph, DocBook requires them within list items.
        break;
    case Atom::ListItemRight:
        if (atom->string() == ATOM_LIST_TAG) {
            writer.writeEndElement(); // para
            newLine(writer);
            writer.writeEndElement(); // listitem
            newLine(writer);
            writer.writeEndElement(); // varlistentry
            newLine(writer);
        } else if (atom->string() == ATOM_LIST_VALUE) {
            if (inListItemLineOpen) {
                writer.writeEndElement(); // td
                newLine(writer);
                inListItemLineOpen = false;
            }
            writer.writeEndElement(); // tr
            newLine(writer);
        } else {
            writer.writeEndElement(); // listitem
            newLine(writer);
        }
        break;
    case Atom::ListRight:
        // Depending on atom->string(), closing a different item:
        // - ATOM_LIST_BULLET: itemizedlist
        // - ATOM_LIST_TAG: variablelist
        // - ATOM_LIST_VALUE: informaltable
        // - ATOM_LIST_NUMERIC: orderedlist
        writer.writeEndElement();
        newLine(writer);
        break;
    case Atom::Nop:
        break;
    case Atom::ParaLeft:
        writer.writeStartElement(dbNamespace, "para");
        inPara = true;
        break;
    case Atom::ParaRight:
        endLink(writer);
        if (inPara) {
            writer.writeEndElement(); // para
            newLine(writer);
            inPara = false;
        }
        break;
    case Atom::QuotationLeft:
        writer.writeStartElement(dbNamespace, "blockquote");
        inPara = true;
        break;
    case Atom::QuotationRight:
        writer.writeEndElement(); // blockquote
        newLine(writer);
        break;
    case Atom::RawString:
        writer.writeCharacters(atom->string());
        break;
    case Atom::SectionLeft:
        currentSectionLevel = atom->string().toInt() + hOffset(relative);
        // Level 1 is dealt with at the header level (info tag).
        if (currentSectionLevel > 1) {
            // Unfortunately, SectionRight corresponds to the end of any section,
            // i.e. going to a new section, even deeper.
            while (!sectionLevels.empty() && sectionLevels.top() >= currentSectionLevel) {
                sectionLevels.pop();
                writer.writeEndElement(); // section
                newLine(writer);
            }

            sectionLevels.push(currentSectionLevel);

            writer.writeStartElement(dbNamespace, "section");
            writer.writeAttribute("xml:id", Doc::canonicalTitle(Text::sectionHeading(atom).toString()));
            newLine(writer);
            // Unlike startSectionBegin, don't start a title here.
        }
        break;
    case Atom::SectionRight:
        // All the logic about closing sections is done in the SectionLeft case
        // and generateFooter() for the end of the page.
        break;
    case Atom::SectionHeadingLeft:
        // Level 1 is dealt with at the header level (info tag).
        if (currentSectionLevel > 1) {
            writer.writeStartElement(dbNamespace, "title");
            inSectionHeading_ = true;
        }
        break;
    case Atom::SectionHeadingRight:
        // Level 1 is dealt with at the header level (info tag).
        if (currentSectionLevel > 1) {
            writer.writeEndElement(); // title
            newLine(writer);
            inSectionHeading_ = false;
        }
        break;
    case Atom::SidebarLeft:
        writer.writeStartElement(dbNamespace, "sidebar");
        break;
    case Atom::SidebarRight:
        writer.writeEndElement(); // sidebar
        newLine(writer);
        break;
    case Atom::String:
        if (inLink && !inContents_ && !inSectionHeading_)
            generateLink(writer, atom);
        else
            writer.writeCharacters(atom->string());
        break;
    case Atom::TableLeft: {
        QPair<QString, QString> pair = getTableWidthAttr(atom);
        QString attr = pair.second;
        QString width = pair.first;

        if (inPara) {
            writer.writeEndElement(); // para or blockquote
            newLine(writer);
            inPara = false;
        }

        writer.writeStartElement(dbNamespace, "informaltable");
        writer.writeAttribute("style", attr);
        if (!width.isEmpty())
            writer.writeAttribute("width", width);
        newLine(writer);
        numTableRows_ = 0;
    } break;
    case Atom::TableRight:
        writer.writeEndElement(); // table
        newLine(writer);
        break;
    case Atom::TableHeaderLeft:
        writer.writeStartElement(dbNamespace, "thead");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "tr");
        newLine(writer);
        inTableHeader_ = true;
        break;
    case Atom::TableHeaderRight:
        writer.writeEndElement(); // tr
        newLine(writer);
        if (matchAhead(atom, Atom::TableHeaderLeft)) {
            skipAhead = 1;
            writer.writeStartElement(dbNamespace, "tr");
            newLine(writer);
        } else {
            writer.writeEndElement(); // thead
            newLine(writer);
            inTableHeader_ = false;
        }
        break;
    case Atom::TableRowLeft:
        writer.writeStartElement(dbNamespace, "tr");
        if (atom->string().isEmpty()) {
            writer.writeAttribute("valign", "top");
        } else {
            // Basic parsing of attributes, should be enough. The input string (atom->string())
            // looks like:
            //      arg1="val1" arg2="val2"
            QStringList args = atom->string().split("\"", Qt::SkipEmptyParts);
            //      arg1=, val1, arg2=, val2,
            //      \-- 1st --/  \-- 2nd --/  \-- remainder
            if (args.size() % 2) {
                // Problem...
                relative->doc().location().warning(
                        tr("Error when parsing attributes for the table: got \"%1\"")
                                .arg(atom->string()));
            }
            for (int i = 0; i + 1 < args.size(); i += 2)
                writer.writeAttribute(args.at(i).chopped(1), args.at(i + 1));
        }
        newLine(writer);
        break;
    case Atom::TableRowRight:
        writer.writeEndElement(); // tr
        newLine(writer);
        break;
    case Atom::TableItemLeft:
        writer.writeStartElement(dbNamespace, inTableHeader_ ? "th" : "td");

        for (int i = 0; i < atom->count(); ++i) {
            const QString &p = atom->string(i);
            if (p.contains('=')) {
                QStringList lp = p.split(QLatin1Char('='));
                writer.writeAttribute(lp.at(0), lp.at(1));
            } else {
                QStringList spans = p.split(QLatin1Char(','));
                if (spans.size() == 2) {
                    if (spans.at(0) != "1")
                        writer.writeAttribute("colspan", spans.at(0));
                    if (spans.at(1) != "1")
                        writer.writeAttribute("rowspan", spans.at(1));
                }
            }
        }
        newLine(writer);
        // No skipahead, as opposed to HTML: in DocBook, the text must be wrapped in paragraphs.
        break;
    case Atom::TableItemRight:
        writer.writeEndElement(); // th if inTableHeader_, otherwise td
        newLine(writer);
        break;
    case Atom::TableOfContents:
        break;
    case Atom::Keyword:
        break;
    case Atom::Target:
        writeAnchor(writer, Doc::canonicalTitle(atom->string()));
        break;
    case Atom::UnhandledFormat:
        writer.writeStartElement(dbNamespace, "emphasis");
        writer.writeAttribute("role", "bold");
        writer.writeCharacters("&lt;Missing DocBook&gt;");
        writer.writeEndElement(); // emphasis
        break;
    case Atom::UnknownCommand:
        writer.writeStartElement(dbNamespace, "emphasis");
        writer.writeAttribute("role", "bold");
        writer.writeCharacters("&lt;Unknown command&gt;");
        writer.writeStartElement(dbNamespace, "code");
        writer.writeCharacters(atom->string());
        writer.writeEndElement(); // code
        writer.writeEndElement(); // emphasis
        break;
    case Atom::QmlText:
    case Atom::EndQmlText:
        // don't do anything with these. They are just tags.
        break;
    case Atom::CodeQuoteArgument:
    case Atom::CodeQuoteCommand:
    case Atom::SnippetCommand:
    case Atom::SnippetIdentifier:
    case Atom::SnippetLocation:
        // no output (ignore)
        break;
    default:
        unknownAtom(atom);
    }
    return skipAhead;
}

void DocBookGenerator::generateClassHierarchy(QXmlStreamWriter &writer, const Node *relative,
                                              NodeMap &classMap)
{
    // From HtmlGenerator::generateClassHierarchy.
    if (classMap.isEmpty())
        return;

    NodeMap topLevel;
    NodeMap::Iterator c = classMap.begin();
    while (c != classMap.end()) {
        auto *classe = static_cast<ClassNode *>(*c);
        if (classe->baseClasses().isEmpty())
            topLevel.insert(classe->name(), classe);
        ++c;
    }

    QStack<NodeMap> stack;
    stack.push(topLevel);

    writer.writeStartElement(dbNamespace, "itemizedlist");
    newLine(writer);
    while (!stack.isEmpty()) {
        if (stack.top().isEmpty()) {
            stack.pop();
            writer.writeEndElement(); // listitem
            newLine(writer);
            writer.writeEndElement(); // itemizedlist
            newLine(writer);
        } else {
            ClassNode *child = static_cast<ClassNode *>(*stack.top().begin());
            writer.writeStartElement(dbNamespace, "listitem");
            newLine(writer);
            writer.writeStartElement(dbNamespace, "para");
            generateFullName(writer, child, relative);
            writer.writeEndElement(); // para
            newLine(writer);
            // Don't close the listitem now, as DocBook requires sublists to reside in items.
            stack.top().erase(stack.top().begin());

            NodeMap newTop;
            for (const RelatedClass &d : child->derivedClasses()) {
                if (d.node_ && !d.isPrivate() && !d.node_->isInternal() && d.node_->hasDoc())
                    newTop.insert(d.node_->name(), d.node_);
            }
            if (!newTop.isEmpty()) {
                stack.push(newTop);
                writer.writeStartElement(dbNamespace, "itemizedlist");
                newLine(writer);
            }
        }
    }
}

void DocBookGenerator::generateLink(QXmlStreamWriter &writer, const Atom *atom)
{
    // From HtmlGenerator::generateLink.
    QRegExp funcLeftParen("\\S(\\()");
    if (funcLeftParen.indexIn(atom->string()) != -1) {
        // hack for C++: move () outside of link
        int k = funcLeftParen.pos(1);
        writer.writeCharacters(atom->string().left(k));
        writer.writeEndElement(); // link
        inLink = false;
        writer.writeCharacters(atom->string().mid(k));
    } else {
        writer.writeCharacters(atom->string());
    }
}

/*!
  This version of the function is called when the \a link is known
  to be correct.
 */
void DocBookGenerator::beginLink(QXmlStreamWriter &writer, const QString &link, const Node *node,
                                 const Node *relative)
{
    // From HtmlGenerator::beginLink.
    writer.writeStartElement(dbNamespace, "link");
    writer.writeAttribute(xlinkNamespace, "href", link);
    if (node && !(relative && node->status() == relative->status())
        && node->status() == Node::Obsolete)
        writer.writeAttribute("role", "obsolete");
    inLink = true;
}

void DocBookGenerator::endLink(QXmlStreamWriter &writer)
{
    // From HtmlGenerator::endLink.
    if (inLink)
        writer.writeEndElement(); // link
    inLink = false;
}

void DocBookGenerator::generateList(QXmlStreamWriter &writer, const Node *relative,
                                    const QString &selector)
{
    // From HtmlGenerator::generateList, without warnings, changing prototype.
    CNMap cnm;
    Node::NodeType type = Node::NoType;
    if (selector == QLatin1String("overviews"))
        type = Node::Group;
    else if (selector == QLatin1String("cpp-modules"))
        type = Node::Module;
    else if (selector == QLatin1String("qml-modules"))
        type = Node::QmlModule;
    else if (selector == QLatin1String("js-modules"))
        type = Node::JsModule;

    if (type != Node::NoType) {
        NodeList nodeList;
        qdb_->mergeCollections(type, cnm, relative);
        const QList<CollectionNode *> collectionList = cnm.values();
        nodeList.reserve(collectionList.size());
        for (auto *collectionNode : collectionList)
            nodeList.append(collectionNode);
        generateAnnotatedList(writer, relative, nodeList, selector);
    } else {
        /*
          \generatelist {selector} is only allowed in a
          comment where the topic is \group, \module,
          \qmlmodule, or \jsmodule
        */
        Node *n = const_cast<Node *>(relative);
        auto *cn = static_cast<CollectionNode *>(n);
        qdb_->mergeCollections(cn);
        generateAnnotatedList(writer, cn, cn->members(), selector);
    }
}

/*!
  Output an annotated list of the nodes in \a nodeMap.
  A two-column table is output.
 */
void DocBookGenerator::generateAnnotatedList(QXmlStreamWriter &writer, const Node *relative,
                                             const NodeMultiMap &nmm, const QString &selector)
{
    // From HtmlGenerator::generateAnnotatedList
    if (nmm.isEmpty())
        return;
    generateAnnotatedList(writer, relative, nmm.values(), selector);
}

void DocBookGenerator::generateAnnotatedList(QXmlStreamWriter &writer, const Node *relative,
                                             const NodeList &nodeList, const QString &selector)
{
    // From WebXMLGenerator::generateAnnotatedList.
    writer.writeStartElement(dbNamespace, "variablelist");
    writer.writeAttribute("role", selector);
    newLine(writer);

    for (auto node : nodeList) {
        writer.writeStartElement(dbNamespace, "varlistentry");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "term");
        generateFullName(writer, node, relative);
        writer.writeEndElement(); // term
        newLine(writer);

        writer.writeStartElement(dbNamespace, "listitem");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "para");
        writer.writeCharacters(node->doc().briefText().toString());
        writer.writeEndElement(); // para
        newLine(writer);
        writer.writeEndElement(); // listitem
        newLine(writer);
        writer.writeEndElement(); // varlistentry
        newLine(writer);
    }
    writer.writeEndElement(); // variablelist
    newLine(writer);
}

/*!
  Outputs a series of annotated lists from the nodes in \a nmm,
  divided into sections based by the key names in the multimap.
 */
void DocBookGenerator::generateAnnotatedLists(QXmlStreamWriter &writer, const Node *relative,
                                              const NodeMultiMap &nmm, const QString &selector)
{
    // From HtmlGenerator::generateAnnotatedLists.
    for (const QString &name : nmm.uniqueKeys()) {
        if (!name.isEmpty())
            startSection(writer, registerRef(name.toLower()), name);
        generateAnnotatedList(writer, relative, nmm.values(name), selector);
        if (!name.isEmpty())
            endSection(writer);
    }
}

/*!
  This function finds the common prefix of the names of all
  the classes in the class map \a nmm and then generates a
  compact list of the class names alphabetized on the part
  of the name not including the common prefix. You can tell
  the function to use \a comonPrefix as the common prefix,
  but normally you let it figure it out itself by looking at
  the name of the first and last classes in the class map
  \a nmm.
 */
void DocBookGenerator::generateCompactList(QXmlStreamWriter &writer, ListType listType,
                                           const Node *relative, const NodeMultiMap &nmm,
                                           const QString &commonPrefix, const QString &selector)
{
    // From HtmlGenerator::generateCompactList. No more "includeAlphabet", this should be handled by
    // the DocBook toolchain afterwards.
    // TODO: In DocBook, probably no need for this method: this is purely presentational, i.e. to be
    // fully handled by the DocBook toolchain.
    if (nmm.isEmpty())
        return;

    const int NumParagraphs = 37; // '0' to '9', 'A' to 'Z', '_'
    int commonPrefixLen = commonPrefix.length();

    /*
      Divide the data into 37 paragraphs: 0, ..., 9, A, ..., Z,
      underscore (_). QAccel will fall in paragraph 10 (A) and
      QXtWidget in paragraph 33 (X). This is the only place where we
      assume that NumParagraphs is 37. Each paragraph is a NodeMultiMap.
    */
    NodeMultiMap paragraph[NumParagraphs + 1];
    QString paragraphName[NumParagraphs + 1];
    QSet<char> usedParagraphNames;

    NodeMultiMap::ConstIterator c = nmm.constBegin();
    while (c != nmm.constEnd()) {
        QStringList pieces = c.key().split("::");
        QString key;
        int idx = commonPrefixLen;
        if (idx > 0 && !pieces.last().startsWith(commonPrefix, Qt::CaseInsensitive))
            idx = 0;
        key = pieces.last().mid(idx).toLower();

        int paragraphNr = NumParagraphs - 1;

        if (key[0].digitValue() != -1)
            paragraphNr = key[0].digitValue();
        else if (key[0] >= QLatin1Char('a') && key[0] <= QLatin1Char('z'))
            paragraphNr = 10 + key[0].unicode() - 'a';

        paragraphName[paragraphNr] = key[0].toUpper();
        usedParagraphNames.insert(key[0].toLower().cell());
        paragraph[paragraphNr].insert(c.key(), c.value());
        ++c;
    }

    /*
      Each paragraph j has a size: paragraph[j].count(). In the
      discussion, we will assume paragraphs 0 to 5 will have sizes
      3, 1, 4, 1, 5, 9.

      We now want to compute the paragraph offset. Paragraphs 0 to 6
      start at offsets 0, 3, 4, 8, 9, 14, 23.
    */
    int paragraphOffset[NumParagraphs + 1]; // 37 + 1
    paragraphOffset[0] = 0;
    for (int i = 0; i < NumParagraphs; i++) // i = 0..36
        paragraphOffset[i + 1] = paragraphOffset[i] + paragraph[i].count();

    // No table of contents in DocBook.

    // Actual output.
    numTableRows_ = 0;

    int curParNr = 0;
    int curParOffset = 0;
    QString previousName;
    bool multipleOccurrences = false;

    for (int i = 0; i < nmm.count(); i++) {
        while ((curParNr < NumParagraphs) && (curParOffset == paragraph[curParNr].count())) {
            ++curParNr;
            curParOffset = 0;
        }

        /*
          Starting a new paragraph means starting a new variablelist.
        */
        if (curParOffset == 0) {
            if (i > 0) {
                writer.writeEndElement(); // variablelist
                newLine(writer);
            }

            writer.writeStartElement(dbNamespace, "variablelist");
            writer.writeAttribute("role", selector);
            newLine(writer);
            writer.writeStartElement(dbNamespace, "varlistentry");
            newLine(writer);

            writer.writeStartElement(dbNamespace, "term");
            writer.writeStartElement(dbNamespace, "emphasis");
            writer.writeAttribute("role", "bold");
            writer.writeCharacters(paragraphName[curParNr]);
            writer.writeEndElement(); // emphasis
            writer.writeEndElement(); // term
            newLine(writer);
        }

        /*
          Output a listitem for the current offset in the current paragraph.
         */
        writer.writeStartElement(dbNamespace, "listitem");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "para");
        if ((curParNr < NumParagraphs) && !paragraphName[curParNr].isEmpty()) {
            NodeMultiMap::Iterator it;
            NodeMultiMap::Iterator next;
            it = paragraph[curParNr].begin();
            for (int j = 0; j < curParOffset; j++)
                ++it;

            if (listType == Generic) {
                generateFullName(writer, it.value(), relative);
                writer.writeStartElement(dbNamespace, "link");
                writer.writeAttribute(xlinkNamespace, "href", fullDocumentLocation(*it));
                writer.writeAttribute("type", targetType(it.value()));
            } else if (listType == Obsolete) {
                QString fn = fileName(it.value(), fileExtension());
                QString link;
                if (useOutputSubdirs())
                    link = QString("../" + it.value()->outputSubdirectory() + QLatin1Char('/'));
                link += fn;

                writer.writeStartElement(dbNamespace, "link");
                writer.writeAttribute(xlinkNamespace, "href", link);
                writer.writeAttribute("type", targetType(it.value()));
            }

            QStringList pieces;
            if (it.value()->isQmlType() || it.value()->isJsType()) {
                QString name = it.value()->name();
                next = it;
                ++next;
                if (name != previousName)
                    multipleOccurrences = false;
                if ((next != paragraph[curParNr].end()) && (name == next.value()->name())) {
                    multipleOccurrences = true;
                    previousName = name;
                }
                if (multipleOccurrences)
                    name += ": " + it.value()->tree()->camelCaseModuleName();
                pieces << name;
            } else
                pieces = it.value()->fullName(relative).split("::");

            writer.writeCharacters(pieces.last());
            writer.writeEndElement(); // link

            if (pieces.size() > 1) {
                writer.writeCharacters(" (");
                generateFullName(writer, it.value()->parent(), relative);
                writer.writeCharacters(")");
            }
        }
        writer.writeEndElement(); // para
        newLine(writer);
        writer.writeEndElement(); // listitem
        newLine(writer);
        writer.writeEndElement(); // varlistentry
        newLine(writer);
        curParOffset++;
    }
    if (nmm.count() > 0) {
        writer.writeEndElement(); // variablelist
    }
}

void DocBookGenerator::generateFunctionIndex(QXmlStreamWriter &writer, const Node *relative)
{
    // From HtmlGenerator::generateFunctionIndex.
    writer.writeStartElement(dbNamespace, "simplelist");
    writer.writeAttribute("role", "functionIndex");
    newLine(writer);
    for (int i = 0; i < 26; i++) {
        QChar ch('a' + i);
        writer.writeStartElement(dbNamespace, "member");
        writer.writeAttribute(xlinkNamespace, "href", QString("#") + ch);
        writer.writeCharacters(ch.toUpper());
        writer.writeEndElement(); // member
        newLine(writer);
    }
    writer.writeEndElement(); // simplelist
    newLine(writer);

    char nextLetter = 'a';
    char currentLetter;

    writer.writeStartElement(dbNamespace, "itemizedlist");
    newLine(writer);

    NodeMapMap &funcIndex = qdb_->getFunctionIndex();
    QMap<QString, NodeMap>::ConstIterator f = funcIndex.constBegin();
    while (f != funcIndex.constEnd()) {
        writer.writeStartElement(dbNamespace, "listitem");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "para");
        writer.writeCharacters(f.key() + ": ");

        currentLetter = f.key()[0].unicode();
        while (islower(currentLetter) && currentLetter >= nextLetter) {
            writeAnchor(writer, QString(nextLetter));
            nextLetter++;
        }

        NodeMap::ConstIterator s = (*f).constBegin();
        while (s != (*f).constEnd()) {
            writer.writeCharacters(" ");
            generateFullName(writer, (*s)->parent(), relative);
            ++s;
        }

        writer.writeEndElement(); // para
        newLine(writer);
        writer.writeEndElement(); // listitem
        newLine(writer);
        ++f;
    }
    writer.writeEndElement(); // itemizedlist
    newLine(writer);
}

void DocBookGenerator::generateLegaleseList(QXmlStreamWriter &writer, const Node *relative)
{
    // From HtmlGenerator::generateLegaleseList.
    TextToNodeMap &legaleseTexts = qdb_->getLegaleseTexts();
    QMap<Text, const Node *>::ConstIterator it = legaleseTexts.constBegin();
    while (it != legaleseTexts.constEnd()) {
        Text text = it.key();
        generateText(writer, text, relative);
        writer.writeStartElement(dbNamespace, "itemizedlist");
        newLine(writer);
        do {
            writer.writeStartElement(dbNamespace, "listitem");
            newLine(writer);
            writer.writeStartElement(dbNamespace, "para");
            generateFullName(writer, it.value(), relative);
            writer.writeEndElement(); // para
            newLine(writer);
            writer.writeEndElement(); // listitem
            newLine(writer);
            ++it;
        } while (it != legaleseTexts.constEnd() && it.key() == text);
        writer.writeEndElement(); // itemizedlist
        newLine(writer);
    }
}

void DocBookGenerator::generateBrief(QXmlStreamWriter &writer, const Node *node)
{
    // From HtmlGenerator::generateBrief. Also see generateHeader, which is specifically dealing
    // with the DocBook header (and thus wraps the brief in an abstract).
    Text brief = node->doc().briefText();

    if (!brief.isEmpty()) {
        if (!brief.lastAtom()->string().endsWith('.'))
            brief << Atom(Atom::String, ".");

        writer.writeStartElement(dbNamespace, "para");
        generateText(writer, brief, node);
        writer.writeEndElement(); // para
        newLine(writer);
    }
}

bool DocBookGenerator::generateSince(QXmlStreamWriter &writer, const Node *node)
{
    // From Generator::generateSince.
    if (!node->since().isEmpty()) {
        writer.writeStartElement(dbNamespace, "para");
        writer.writeCharacters("This " + typeString(node) + " was introduced");
        if (node->nodeType() == Node::Enum)
            writer.writeCharacters(" or modified");
        writer.writeCharacters(" in " + formatSince(node) + ".");
        writer.writeEndElement(); // para
        newLine(writer);

        return true;
    }

    return false;
}

void DocBookGenerator::generateHeader(QXmlStreamWriter &writer, const QString &title,
                                      const QString &subTitle, const Node *node)
{
    // From HtmlGenerator::generateHeader.
    refMap.clear();

    // Output the DocBook header.
    writer.writeStartElement(dbNamespace, "info");
    newLine(writer);
    writer.writeTextElement(dbNamespace, "title", title);
    newLine(writer);

    if (!subTitle.isEmpty()) {
        writer.writeTextElement(dbNamespace, "subtitle", subTitle);
        newLine(writer);
    }

    if (!project.isEmpty()) {
        writer.writeTextElement(dbNamespace, "productname", project);
        newLine(writer);
    }

    if (!buildversion.isEmpty()) {
        writer.writeTextElement(dbNamespace, "edition", buildversion);
        newLine(writer);
    }

    if (!projectDescription.isEmpty()) {
        writer.writeTextElement(dbNamespace, "titleabbrev", projectDescription);
        newLine(writer);
    }

    // Deal with links.
    // Adapted from HtmlGenerator::generateHeader (output part: no need to update a navigationLinks
    // or useSeparator field, as this content is only output in the info tag, not in the main
    // content).
    if (node && !node->links().empty()) {
        QPair<QString, QString> linkPair;
        QPair<QString, QString> anchorPair;
        const Node *linkNode;

        if (node->links().contains(Node::PreviousLink)) {
            linkPair = node->links()[Node::PreviousLink];
            linkNode = qdb_->findNodeForTarget(linkPair.first, node);
            if (!linkNode || linkNode == node)
                anchorPair = linkPair;
            else
                anchorPair = anchorForNode(linkNode);

            writer.writeStartElement(dbNamespace, "extendedlink");
            writer.writeEmptyElement(dbNamespace, "link");
            writer.writeAttribute(xlinkNamespace, "to", anchorPair.first);
            writer.writeAttribute(xlinkNamespace, "title", "prev");
            if (linkPair.first == linkPair.second && !anchorPair.second.isEmpty())
                writer.writeAttribute(xlinkNamespace, "label", anchorPair.second);
            else
                writer.writeAttribute(xlinkNamespace, "label", linkPair.second);
            writer.writeEndElement(); // extendedlink
        }
        if (node->links().contains(Node::NextLink)) {
            linkPair = node->links()[Node::NextLink];
            linkNode = qdb_->findNodeForTarget(linkPair.first, node);
            if (!linkNode || linkNode == node)
                anchorPair = linkPair;
            else
                anchorPair = anchorForNode(linkNode);

            writer.writeStartElement(dbNamespace, "extendedlink");
            writer.writeEmptyElement(dbNamespace, "link");
            writer.writeAttribute(xlinkNamespace, "to", anchorPair.first);
            writer.writeAttribute(xlinkNamespace, "title", "prev");
            if (linkPair.first == linkPair.second && !anchorPair.second.isEmpty())
                writer.writeAttribute(xlinkNamespace, "label", anchorPair.second);
            else
                writer.writeAttribute(xlinkNamespace, "label", linkPair.second);
            writer.writeEndElement(); // extendedlink
        }
        if (node->links().contains(Node::StartLink)) {
            linkPair = node->links()[Node::StartLink];
            linkNode = qdb_->findNodeForTarget(linkPair.first, node);
            if (!linkNode || linkNode == node)
                anchorPair = linkPair;
            else
                anchorPair = anchorForNode(linkNode);

            writer.writeStartElement(dbNamespace, "extendedlink");
            writer.writeEmptyElement(dbNamespace, "link");
            writer.writeAttribute(xlinkNamespace, "to", anchorPair.first);
            writer.writeAttribute(xlinkNamespace, "title", "start");
            if (linkPair.first == linkPair.second && !anchorPair.second.isEmpty())
                writer.writeAttribute(xlinkNamespace, "label", anchorPair.second);
            else
                writer.writeAttribute(xlinkNamespace, "label", linkPair.second);
            writer.writeEndElement(); // extendedlink
        }
    }

    // Deal with the abstract (what qdoc calls brief).
    if (node) {
        // Adapted from HtmlGenerator::generateBrief, without extraction marks. The parameter
        // addLink is always false. Factoring this function out is not as easy as in HtmlGenerator:
        // abstracts only happen in the header (info tag), slightly different tags must be used at
        // other places. Also includes code from HtmlGenerator::generateCppReferencePage to handle
        // the name spaces.
        writer.writeStartElement(dbNamespace, "abstract");
        newLine(writer);

        bool generatedSomething = false;

        Text brief;
        const NamespaceNode *ns = node->isAggregate()
                ? static_cast<const NamespaceNode *>(static_cast<const Aggregate *>(node))
                : nullptr;
        if (node->isAggregate() && ns && !ns->hasDoc() && ns->docNode()) {
            NamespaceNode *NS = ns->docNode();
            brief << "The " << ns->name()
                  << " namespace includes the following elements from module "
                  << ns->tree()->camelCaseModuleName() << ". The full namespace is "
                  << "documented in module " << NS->tree()->camelCaseModuleName()
                  << Atom(Atom::LinkNode, fullDocumentLocation(NS))
                  << Atom(Atom::FormattingLeft, ATOM_FORMATTING_LINK)
                  << Atom(Atom::String, " here.")
                  << Atom(Atom::FormattingRight, ATOM_FORMATTING_LINK);
        } else {
            brief = node->doc().briefText();
        }

        if (!brief.isEmpty()) {
            if (!brief.lastAtom()->string().endsWith('.'))
                brief << Atom(Atom::String, ".");

            writer.writeStartElement(dbNamespace, "para");
            generateText(writer, brief, node);
            writer.writeEndElement(); // para
            newLine(writer);

            generatedSomething = true;
        }

        // Generate other paragraphs that should go into the abstract.
        generatedSomething |= generateStatus(writer, node);
        generatedSomething |= generateSince(writer, node);
        generatedSomething |= generateThreadSafeness(writer, node);

        // An abstract cannot be empty, hence use the project description.
        if (!generatedSomething)
            writer.writeTextElement(dbNamespace, "para", projectDescription + ".");

        writer.writeEndElement(); // abstract
        newLine(writer);
    }

    // End of the DocBook header.
    writer.writeEndElement(); // info
    newLine(writer);
}

void DocBookGenerator::closeTextSections(QXmlStreamWriter &writer)
{
    while (!sectionLevels.isEmpty()) {
        sectionLevels.pop();
        endSection(writer);
    }
}

void DocBookGenerator::generateFooter(QXmlStreamWriter &writer)
{
    closeTextSections(writer);
    writer.writeEndElement(); // article
}

static void generateSimpleLink(QXmlStreamWriter &writer, const QString &href, const QString &text)
{
    writer.writeStartElement(dbNamespace, "link");
    writer.writeAttribute(xlinkNamespace, "href", href);
    writer.writeCharacters(text);
    writer.writeEndElement(); // link
}

void DocBookGenerator::generateObsoleteMembers(QXmlStreamWriter &writer, const Sections &sections)
{
    // From HtmlGenerator::generateObsoleteMembersFile.
    SectionPtrVector summary_spv; // Summaries are ignored in DocBook (table of contents).
    SectionPtrVector details_spv;
    if (!sections.hasObsoleteMembers(&summary_spv, &details_spv))
        return;

    Aggregate *aggregate = sections.aggregate();
    QString link;
    if (useOutputSubdirs() && !Generator::outputSubdir().isEmpty())
        link = QString("../" + Generator::outputSubdir() + QLatin1Char('/'));
    link += fileName(aggregate, fileExtension());
    aggregate->setObsoleteLink(link);

    startSection(writer, "obsolete", "Obsolete Members for " + aggregate->name());

    writer.writeStartElement(dbNamespace, "para");
    writer.writeStartElement(dbNamespace, "emphasis");
    writer.writeAttribute("role", "bold");
    writer.writeCharacters("The following members of class ");
    generateSimpleLink(writer, linkForNode(aggregate, nullptr), aggregate->name());
    writer.writeCharacters(" are obsolete.");
    writer.writeEndElement(); // emphasis bold
    writer.writeCharacters(" They are provided to keep old source code working. "
                           "We strongly advise against using them in new code.");
    writer.writeEndElement(); // para
    newLine(writer);

    for (int i = 0; i < details_spv.size(); ++i) {
        QString title = details_spv.at(i)->title();
        QString ref = registerRef(title.toLower());
        startSection(writer, ref, title);

        const NodeVector &members = details_spv.at(i)->obsoleteMembers();
        NodeVector::ConstIterator m = members.constBegin();
        while (m != members.constEnd()) {
            if ((*m)->access() != Node::Private)
                generateDetailedMember(writer, *m, aggregate);
            ++m;
        }

        endSection(writer);
    }

    endSection(writer);
}

/*!
  Generates a separate file where obsolete members of the QML
  type \a qcn are listed. The \a marker is used to generate
  the section lists, which are then traversed and output here.

  Note that this function currently only handles correctly the
  case where \a status is \c {Section::Obsolete}.
 */
void DocBookGenerator::generateObsoleteQmlMembers(QXmlStreamWriter &writer,
                                                  const Sections &sections)
{
    // From HtmlGenerator::generateObsoleteQmlMembersFile.
    SectionPtrVector summary_spv; // Summaries are not useful in DocBook.
    SectionPtrVector details_spv;
    if (!sections.hasObsoleteMembers(&summary_spv, &details_spv))
        return;

    Aggregate *aggregate = sections.aggregate();
    QString title = "Obsolete Members for " + aggregate->name();
    QString fn = fileName(aggregate, fileExtension());
    QString link;
    if (useOutputSubdirs() && !Generator::outputSubdir().isEmpty())
        link = QString("../" + Generator::outputSubdir() + QLatin1Char('/'));
    link += fn;
    aggregate->setObsoleteLink(link);

    startSection(writer, "obsolete", "Obsolete Members for " + aggregate->name());

    writer.writeStartElement(dbNamespace, "para");
    writer.writeStartElement(dbNamespace, "emphasis");
    writer.writeAttribute("role", "bold");
    writer.writeCharacters("The following members of QML type ");
    generateSimpleLink(writer, linkForNode(aggregate, nullptr), aggregate->name());
    writer.writeCharacters(" are obsolete.");
    writer.writeEndElement(); // emphasis bold
    writer.writeCharacters("They are provided to keep old source code working. "
                           "We strongly advise against using them in new code.");
    writer.writeEndElement(); // para
    newLine(writer);

    for (auto i : details_spv) {
        QString ref = registerRef(i->title().toLower());
        startSection(writer, ref, i->title());

        NodeVector::ConstIterator m = i->members().constBegin();
        while (m != i->members().constEnd()) {
            generateDetailedQmlMember(writer, *m, aggregate);
            ++m;
        }

        endSection(writer);
    }

    endSection(writer);
}

static QString nodeToSynopsisTag(const Node *node)
{
    // Order from Node::nodeTypeString.
    if (node->isClass() || node->isQmlType() || node->isQmlBasicType())
        return QStringLiteral("classsynopsis");
    if (node->isNamespace())
        return QStringLiteral("namespacesynopsis");
    if (node->isPageNode()) {
        node->doc().location().warning("Unexpected document node in nodeToSynopsisTag");
        return QString();
    }
    if (node->isEnumType())
        return QStringLiteral("enumsynopsis");
    if (node->isTypedef())
        return QStringLiteral("typedefsynopsis");
    if (node->isFunction()) {
        // Signals are also encoded as functions (including QML/JS ones).
        const auto fn = static_cast<const FunctionNode *>(node);
        if (fn->isCtor() || fn->isCCtor() || fn->isMCtor())
            return QStringLiteral("constructorsynopsis");
        if (fn->isDtor())
            return QStringLiteral("destructorsynopsis");
        return QStringLiteral("methodsynopsis");
    }
    if (node->isProperty() || node->isVariable() || node->isQmlProperty())
        return QStringLiteral("fieldsynopsis");

    node->doc().location().warning(QString("Unknown node tag %1").arg(node->nodeTypeString()));
    return QStringLiteral("synopsis");
}

void generateStartRequisite(QXmlStreamWriter &writer, const QString &description)
{
    writer.writeStartElement(dbNamespace, "varlistentry");
    newLine(writer);
    writer.writeTextElement(dbNamespace, "term", description);
    newLine(writer);
    writer.writeStartElement(dbNamespace, "listitem");
    newLine(writer);
    writer.writeStartElement(dbNamespace, "para");
}

void generateEndRequisite(QXmlStreamWriter &writer)
{
    writer.writeEndElement(); // para
    newLine(writer);
    writer.writeEndElement(); // listitem
    newLine(writer);
    writer.writeEndElement(); // varlistentry
    newLine(writer);
}

void generateRequisite(QXmlStreamWriter &writer, const QString &description, const QString &value)
{
    generateStartRequisite(writer, description);
    writer.writeCharacters(value);
    generateEndRequisite(writer);
}

void DocBookGenerator::generateSortedNames(QXmlStreamWriter &writer, const ClassNode *cn,
                                           const QVector<RelatedClass> &rc)
{
    // From Generator::appendSortedNames.
    QMap<QString, ClassNode *> classMap;
    QVector<RelatedClass>::ConstIterator r = rc.constBegin();
    while (r != rc.constEnd()) {
        ClassNode *rcn = (*r).node_;
        if (rcn && rcn->access() == Node::Public && rcn->status() != Node::Internal
            && !rcn->doc().isEmpty()) {
            classMap[rcn->plainFullName(cn).toLower()] = rcn;
        }
        ++r;
    }

    QStringList classNames = classMap.keys();
    classNames.sort();

    int index = 0;
    for (const QString &className : classNames) {
        generateFullName(writer, classMap.value(className), cn);
        writer.writeCharacters(comma(index++, classNames.count()));
    }
}

void DocBookGenerator::generateSortedQmlNames(QXmlStreamWriter &writer, const Node *base,
                                              const NodeList &subs)
{
    // From Generator::appendSortedQmlNames.
    QMap<QString, Node *> classMap;
    int index = 0;

    for (auto sub : subs)
        if (!base->isQtQuickNode() || !sub->isQtQuickNode()
            || (base->logicalModuleName() == sub->logicalModuleName()))
            classMap[sub->plainFullName(base).toLower()] = sub;

    QStringList names = classMap.keys();
    names.sort();

    for (const QString &name : names) {
        generateFullName(writer, classMap.value(name), base);
        writer.writeCharacters(comma(index++, names.count()));
    }
}

/*!
  Lists the required imports and includes.
*/
void DocBookGenerator::generateRequisites(QXmlStreamWriter &writer, const Aggregate *aggregate)
{
    // Adapted from HtmlGenerator::generateRequisites, but simplified: no need to store all the
    // elements, they can be produced one by one.
    writer.writeStartElement(dbNamespace, "variablelist");
    newLine(writer);

    // Includes.
    if (!aggregate->includeFiles().isEmpty()) {
        for (const QString &include : aggregate->includeFiles())
            generateRequisite(writer, "Header", include);
    }

    // Since and project.
    if (!aggregate->since().isEmpty())
        generateRequisite(writer, "Since", formatSince(aggregate));

    if (aggregate->isClassNode() || aggregate->isNamespace()) {
        // QT variable.
        if (!aggregate->physicalModuleName().isEmpty()) {
            const CollectionNode *cn =
                    qdb_->getCollectionNode(aggregate->physicalModuleName(), Node::Module);
            if (cn && !cn->qtVariable().isEmpty()) {
                generateRequisite(writer, "qmake", "QT += " + cn->qtVariable());
            }
        }
    }

    if (aggregate->nodeType() == Node::Class) {
        // Instantiated by.
        auto *classe = const_cast<ClassNode *>(static_cast<const ClassNode *>(aggregate));
        if (classe->qmlElement() != nullptr && classe->status() != Node::Internal) {
            generateStartRequisite(writer, "Inherited By");
            generateSortedNames(writer, classe, classe->derivedClasses());
            generateEndRequisite(writer);
            generateRequisite(writer, "Instantiated By",
                              fullDocumentLocation(classe->qmlElement()));
        }

        // Inherits.
        QVector<RelatedClass>::ConstIterator r;
        if (!classe->baseClasses().isEmpty()) {
            generateStartRequisite(writer, "Inherits");

            r = classe->baseClasses().constBegin();
            int index = 0;
            while (r != classe->baseClasses().constEnd()) {
                if ((*r).node_) {
                    generateFullName(writer, (*r).node_, classe);

                    if ((*r).access_ == Node::Protected)
                        writer.writeCharacters(" (protected)");
                    else if ((*r).access_ == Node::Private)
                        writer.writeCharacters(" (private)");
                    writer.writeCharacters(comma(index++, classe->baseClasses().count()));
                }
                ++r;
            }

            generateEndRequisite(writer);
        }

        // Inherited by.
        if (!classe->derivedClasses().isEmpty()) {
            generateStartRequisite(writer, "Inherited By");
            generateSortedNames(writer, classe, classe->derivedClasses());
            generateEndRequisite(writer);
        }
    }

    writer.writeEndElement(); // variablelist
    newLine(writer);
}

/*!
  Lists the required imports and includes.
*/
void DocBookGenerator::generateQmlRequisites(QXmlStreamWriter &writer, const QmlTypeNode *qcn)
{
    // From HtmlGenerator::generateQmlRequisites, but simplified: no need to store all the elements,
    // they can be produced one by one.
    if (!qcn)
        return;

    writer.writeStartElement(dbNamespace, "variablelist");
    newLine(writer);

    // Module name and version (i.e. import).
    QString logicalModuleVersion;
    const CollectionNode *collection =
            qdb_->getCollectionNode(qcn->logicalModuleName(), qcn->nodeType());
    if (collection)
        logicalModuleVersion = collection->logicalModuleVersion();
    else
        logicalModuleVersion = qcn->logicalModuleVersion();

    generateRequisite(writer, "Import Statement",
                      "import " + qcn->logicalModuleName() + QLatin1Char(' ')
                              + logicalModuleVersion);

    // Since and project.
    if (!qcn->since().isEmpty())
        generateRequisite(writer, "Since:", formatSince(qcn));

    // Inherited by.
    NodeList subs;
    QmlTypeNode::subclasses(qcn, subs);
    if (!subs.isEmpty()) {
        generateStartRequisite(writer, "Inherited By:");
        generateSortedQmlNames(writer, qcn, subs);
        generateEndRequisite(writer);
    }

    // Inherits.
    QmlTypeNode *base = qcn->qmlBaseNode();
    while (base && base->isInternal()) {
        base = base->qmlBaseNode();
    }
    if (base) {
        const Node *otherNode = nullptr;
        Atom a = Atom(Atom::LinkNode, CodeMarker::stringForNode(base));
        QString link = getAutoLink(&a, qcn, &otherNode);

        generateStartRequisite(writer, "Inherits:");
        generateSimpleLink(writer, link, base->name());
        generateEndRequisite(writer);
    }

    // Instantiates.
    ClassNode *cn = (const_cast<QmlTypeNode *>(qcn))->classNode();
    if (cn && (cn->status() != Node::Internal)) {
        const Node *otherNode = nullptr;
        Atom a = Atom(Atom::LinkNode, CodeMarker::stringForNode(qcn));
        QString link = getAutoLink(&a, cn, &otherNode);

        generateStartRequisite(writer, "Instantiates:");
        generateSimpleLink(writer, fullDocumentLocation(cn), cn->name());
        generateEndRequisite(writer);
    }

    writer.writeEndElement(); // variablelist
    newLine(writer);
}

bool DocBookGenerator::generateStatus(QXmlStreamWriter &writer, const Node *node)
{
    // From Generator::generateStatus.
    switch (node->status()) {
    case Node::Active:
        // Do nothing.
        return false;
    case Node::Preliminary:
        writer.writeStartElement(dbNamespace, "para");
        writer.writeStartElement(dbNamespace, "emphasis");
        writer.writeAttribute("role", "bold");
        writer.writeCharacters("This " + typeString(node)
                               + " is under development and is subject to change.");
        writer.writeEndElement(); // emphasis
        writer.writeEndElement(); // para
        newLine(writer);
        return true;
    case Node::Deprecated:
        writer.writeStartElement(dbNamespace, "para");
        if (node->isAggregate()) {
            writer.writeStartElement(dbNamespace, "emphasis");
            writer.writeAttribute("role", "bold");
        }
        writer.writeCharacters("This " + typeString(node) + " is deprecated.");
        if (node->isAggregate())
            writer.writeEndElement(); // emphasis
        writer.writeEndElement(); // para
        newLine(writer);
        return true;
    case Node::Obsolete:
        writer.writeStartElement(dbNamespace, "para");
        if (node->isAggregate()) {
            writer.writeStartElement(dbNamespace, "emphasis");
            writer.writeAttribute("role", "bold");
        }
        writer.writeCharacters("This " + typeString(node) + " is obsolete.");
        if (node->isAggregate())
            writer.writeEndElement(); // emphasis
        writer.writeCharacters(" It is provided to keep old source code working. "
                               "We strongly advise against using it in new code.");
        writer.writeEndElement(); // para
        newLine(writer);
        return true;
    case Node::Internal:
    default:
        return false;
    }
}

/*!
  Generate a list of function signatures. The function nodes
  are in \a nodes.
 */
void DocBookGenerator::generateSignatureList(QXmlStreamWriter &writer, const NodeList &nodes)
{
    // From Generator::signatureList and Generator::appendSignature.
    writer.writeStartElement(dbNamespace, "itemizedlist");
    newLine(writer);

    NodeList::ConstIterator n = nodes.constBegin();
    while (n != nodes.constEnd()) {
        writer.writeStartElement(dbNamespace, "listitem");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "para");

        generateSimpleLink(writer, currentGenerator()->fullDocumentLocation(*n), (*n)->signature(false, true));

        writer.writeEndElement(); // para
        newLine(writer);
        writer.writeEndElement(); // itemizedlist
        newLine(writer);
        ++n;
    }

    writer.writeEndElement(); // itemizedlist
    newLine(writer);
}

/*!
  Generates text that explains how threadsafe and/or reentrant
  \a node is.
 */
bool DocBookGenerator::generateThreadSafeness(QXmlStreamWriter &writer, const Node *node)
{
    // From Generator::generateThreadSafeness
    Node::ThreadSafeness ts = node->threadSafeness();

    const Node *reentrantNode;
    Atom reentrantAtom = Atom(Atom::Link, "reentrant");
    QString linkReentrant = getAutoLink(&reentrantAtom, node, &reentrantNode);
    const Node *threadSafeNode;
    Atom threadSafeAtom = Atom(Atom::Link, "thread-safe");
    QString linkThreadSafe = getAutoLink(&threadSafeAtom, node, &threadSafeNode);

    if (ts == Node::NonReentrant) {
        writer.writeStartElement(dbNamespace, "warning");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "para");
        writer.writeCharacters("This " + typeString(node) + " is not ");
        generateSimpleLink(writer, linkReentrant, "reentrant");
        writer.writeCharacters(".");
        writer.writeEndElement(); // para
        newLine(writer);
        writer.writeEndElement(); // warning

        return true;
    }
    if (ts == Node::Reentrant || ts == Node::ThreadSafe) {
        writer.writeStartElement(dbNamespace, "note");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "para");

        if (node->isAggregate()) {
            writer.writeCharacters("All functions in this " + typeString(node) + " are ");
            if (ts == Node::ThreadSafe)
                generateSimpleLink(writer, linkThreadSafe, "thread-safe");
            else
                generateSimpleLink(writer, linkReentrant, "reentrant");

            NodeList reentrant;
            NodeList threadsafe;
            NodeList nonreentrant;
            bool exceptions = hasExceptions(node, reentrant, threadsafe, nonreentrant);
            if (!exceptions || (ts == Node::Reentrant && !threadsafe.isEmpty())) {
                writer.writeCharacters(".");
                writer.writeEndElement(); // para
                newLine(writer);
            } else {
                writer.writeCharacters(" with the following exceptions:");
                writer.writeEndElement(); // para
                newLine(writer);
                writer.writeStartElement(dbNamespace, "para");

                if (ts == Node::Reentrant) {
                    if (!nonreentrant.isEmpty()) {
                        writer.writeCharacters("These functions are not ");
                        generateSimpleLink(writer, linkReentrant, "reentrant");
                        writer.writeCharacters(":");
                        writer.writeEndElement(); // para
                        newLine(writer);
                        generateSignatureList(writer, nonreentrant);
                    }
                    if (!threadsafe.isEmpty()) {
                        writer.writeCharacters("These functions are also ");
                        generateSimpleLink(writer, linkThreadSafe, "thread-safe");
                        writer.writeCharacters(":");
                        writer.writeEndElement(); // para
                        newLine(writer);
                        generateSignatureList(writer, threadsafe);
                    }
                } else { // thread-safe
                    if (!reentrant.isEmpty()) {
                        writer.writeCharacters("These functions are only ");
                        generateSimpleLink(writer, linkReentrant, "reentrant");
                        writer.writeCharacters(":");
                        writer.writeEndElement(); // para
                        newLine(writer);
                        generateSignatureList(writer, reentrant);
                    }
                    if (!nonreentrant.isEmpty()) {
                        writer.writeCharacters("These functions are not ");
                        generateSimpleLink(writer, linkReentrant, "reentrant");
                        writer.writeCharacters(":");
                        writer.writeEndElement(); // para
                        newLine(writer);
                        generateSignatureList(writer, nonreentrant);
                    }
                }
            }
        } else {
            writer.writeCharacters("This " + typeString(node) + " is ");
            if (ts == Node::ThreadSafe)
                generateSimpleLink(writer, linkThreadSafe, "thread-safe");
            else
                generateSimpleLink(writer, linkReentrant, "reentrant");
            writer.writeCharacters(".");
            writer.writeEndElement(); // para
            newLine(writer);
        }
        writer.writeEndElement(); // note

        return true;
    }

    return false;
}

/*!
  Generate the body of the documentation from the qdoc comment
  found with the entity represented by the \a node.
 */
void DocBookGenerator::generateBody(QXmlStreamWriter &writer, const Node *node)
{
    // From Generator::generateBody, without warnings.
    if (!node->hasDoc() && !node->hasSharedDoc()) {
        /*
          Test for special function, like a destructor or copy constructor,
          that has no documentation.
        */
        if (node->nodeType() == Node::Function) {
            const auto func = static_cast<const FunctionNode *>(node);
            QString t;
            if (func->isDtor()) {
                t = "Destroys the instance of " + func->parent()->name() + ".";
                if (func->isVirtual())
                    t += " The destructor is virtual.";
            } else if (func->isCtor()) {
                t = "Default constructs an instance of " + func->parent()->name() + ".";
            } else if (func->isCCtor()) {
                t = "Copy constructor.";
            } else if (func->isMCtor()) {
                t = "Move-copy constructor.";
            } else if (func->isCAssign()) {
                t = "Copy-assignment constructor.";
            } else if (func->isMAssign()) {
                t = "Move-assignment constructor.";
            }

            if (!t.isEmpty())
                writer.writeTextElement(dbNamespace, "para", t);
        }
    } else if (!node->isSharingComment()) {
        if (node->nodeType() == Node::Function) {
            const auto func = static_cast<const FunctionNode *>(node);
            if (!func->overridesThis().isEmpty())
                generateReimplementsClause(writer, func);
        }

        if (!generateText(writer, node->doc().body(), node)) {
            if (node->isMarkedReimp())
                return;
        }

        // Warning generation skipped with respect to Generator::generateBody.
    }

    generateRequiredLinks(writer, node);
}

/*!
  Generates either a link to the project folder for example \a node, or a list
  of links files/images if 'url.examples config' variable is not defined.

  Does nothing for non-example nodes.
*/
void DocBookGenerator::generateRequiredLinks(QXmlStreamWriter &writer, const Node *node)
{
    // From Generator::generateRequiredLinks.
    if (!node->isExample())
        return;

    const auto en = static_cast<const ExampleNode *>(node);
    QString exampleUrl = Config::instance().getString(CONFIG_URL + Config::dot + CONFIG_EXAMPLES);

    if (exampleUrl.isEmpty()) {
        if (!en->noAutoList()) {
            generateFileList(writer, en, false); // files
            generateFileList(writer, en, true); // images
        }
    } else {
        generateLinkToExample(writer, en, exampleUrl);
    }
}

/*!
  The path to the example replaces a placeholder '\1' character if
  one is found in the \a baseUrl string.  If no such placeholder is found,
  the path is appended to \a baseUrl, after a '/' character if \a baseUrl did
  not already end in one.
*/
void DocBookGenerator::generateLinkToExample(QXmlStreamWriter &writer, const ExampleNode *en,
                                             const QString &baseUrl)
{
    // From Generator::generateLinkToExample.
    QString exampleUrl(baseUrl);
    QString link;
#ifndef QT_BOOTSTRAPPED
    link = QUrl(exampleUrl).host();
#endif
    if (!link.isEmpty())
        link.prepend(" @ ");
    link.prepend("Example project");

    const QLatin1Char separator('/');
    const QLatin1Char placeholder('\1');
    if (!exampleUrl.contains(placeholder)) {
        if (!exampleUrl.endsWith(separator))
            exampleUrl += separator;
        exampleUrl += placeholder;
    }

    // Construct a path to the example; <install path>/<example name>
    QStringList path = QStringList()
            << Config::instance().getString(CONFIG_EXAMPLESINSTALLPATH) << en->name();
    path.removeAll({});

    writer.writeStartElement(dbNamespace, "para");
    writer.writeStartElement(dbNamespace, "link");
    writer.writeAttribute(xlinkNamespace, "href",
                          exampleUrl.replace(placeholder, path.join(separator)));
    writer.writeCharacters(link);
    writer.writeEndElement(); // link
    writer.writeEndElement(); // para
    newLine(writer);
}

/*!
  This function is called when the documentation for an example is
  being formatted. It outputs a list of files for the example, which
  can be the example's source files or the list of images used by the
  example. The images are copied into a subtree of
  \c{...doc/html/images/used-in-examples/...}
*/
void DocBookGenerator::generateFileList(QXmlStreamWriter &writer, const ExampleNode *en,
                                        bool images)
{
    // From Generator::generateFileList
    QString tag;
    QStringList paths;
    if (images) {
        paths = en->images();
        tag = "Images:";
    } else { // files
        paths = en->files();
        tag = "Files:";
    }
    std::sort(paths.begin(), paths.end(), Generator::comparePaths);

    if (paths.isEmpty())
        return;

    writer.writeStartElement(dbNamespace, "para");
    writer.writeCharacters(tag);
    writer.writeEndElement(); // para
    newLine(writer);

    writer.writeStartElement(dbNamespace, "itemizedlist");

    for (const auto &file : qAsConst(paths)) {
        if (images) {
            if (!file.isEmpty())
                addImageToCopy(en, file);
        } else {
            generateExampleFilePage(en, file);
        }

        writer.writeStartElement(dbNamespace, "listitem");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "para");
        generateSimpleLink(writer, file, file);
        writer.writeEndElement(); // para
        writer.writeEndElement(); // listitem
        newLine(writer);
    }

    writer.writeEndElement(); // itemizedlist
    newLine(writer);
}

/*!
  Generate a file with the contents of a C++ or QML source file.
 */
void DocBookGenerator::generateExampleFilePage(const Node *node, const QString &file)
{
    // From HtmlGenerator::generateExampleFilePage.
    if (!node->isExample())
        return;

    const auto en = static_cast<const ExampleNode *>(node);
    QXmlStreamWriter *writer = startDocument(en, file);
    generateHeader(*writer, en->fullTitle(), en->subtitle(), en);

    Text text;
    Quoter quoter;
    Doc::quoteFromFile(en->doc().location(), quoter, file);
    QString code = quoter.quoteTo(en->location(), QString(), QString());
    CodeMarker *codeMarker = CodeMarker::markerForFileName(file);
    text << Atom(codeMarker->atomType(), code);
    Atom a(codeMarker->atomType(), code);
    generateText(*writer, text, en);

    endDocument(writer);
}

void DocBookGenerator::generateReimplementsClause(QXmlStreamWriter &writer, const FunctionNode *fn)
{
    // From Generator::generateReimplementsClause, without warning generation.
    if (!fn->overridesThis().isEmpty()) {
        if (fn->parent()->isClassNode()) {
            auto cn = static_cast<ClassNode *>(fn->parent());
            const FunctionNode *overrides = cn->findOverriddenFunction(fn);
            if (overrides && !overrides->isPrivate() && !overrides->parent()->isPrivate()) {
                if (overrides->hasDoc()) {
                    writer.writeStartElement(dbNamespace, "para");
                    writer.writeCharacters("Reimplements: ");
                    QString fullName =
                            overrides->parent()->name() + "::" + overrides->signature(false, true);
                    generateFullName(writer, overrides->parent(), fullName, overrides);
                    writer.writeCharacters(".");
                    return;
                }
            }
            const PropertyNode *sameName = cn->findOverriddenProperty(fn);
            if (sameName && sameName->hasDoc()) {
                writer.writeStartElement(dbNamespace, "para");
                writer.writeCharacters("Reimplements an access function for property: ");
                QString fullName = sameName->parent()->name() + "::" + sameName->name();
                generateFullName(writer, sameName->parent(), fullName, overrides);
                writer.writeCharacters(".");
                return;
            }
        }
    }
}

void DocBookGenerator::generateAlsoList(QXmlStreamWriter &writer, const Node *node)
{
    // From Generator::generateAlsoList.
    QVector<Text> alsoList = node->doc().alsoList();
    supplementAlsoList(node, alsoList);

    if (!alsoList.isEmpty()) {
        writer.writeStartElement(dbNamespace, "para");
        writer.writeStartElement(dbNamespace, "emphasis");
        writer.writeCharacters("See also ");
        writer.writeEndElement(); // emphasis
        newLine(writer);

        writer.writeStartElement(dbNamespace, "simplelist");
        writer.writeAttribute("type", "vert");
        writer.writeAttribute("role", "see-also");
        for (const Text &text : alsoList) {
            writer.writeStartElement(dbNamespace, "member");
            generateText(writer, text, node);
            writer.writeEndElement(); // member
            newLine(writer);
        }
        writer.writeEndElement(); // simplelist
        newLine(writer);

        writer.writeEndElement(); // para
    }
}

/*!
  Generate a list of maintainers in the output
 */
void DocBookGenerator::generateMaintainerList(QXmlStreamWriter &writer, const Aggregate *node)
{
    // From Generator::generateMaintainerList.
    QStringList sl = getMetadataElements(node, "maintainer");

    if (!sl.isEmpty()) {
        writer.writeStartElement(dbNamespace, "para");
        writer.writeStartElement(dbNamespace, "emphasis");
        writer.writeCharacters("Maintained by: ");
        writer.writeEndElement(); // emphasis
        newLine(writer);

        writer.writeStartElement(dbNamespace, "simplelist");
        writer.writeAttribute("type", "vert");
        writer.writeAttribute("role", "maintainer");
        for (int i = 0; i < sl.size(); ++i) {
            writer.writeStartElement(dbNamespace, "member");
            writer.writeCharacters(sl.at(i));
            writer.writeEndElement(); // member
            newLine(writer);
        }
        writer.writeEndElement(); // simplelist
        newLine(writer);

        writer.writeEndElement(); // para
    }
}

/*!
  Open a new file to write XML contents, including the DocBook
  opening tag.
 */
QXmlStreamWriter *DocBookGenerator::startGenericDocument(const Node *node, const QString &fileName)
{
    QFile *outFile = openSubPageFile(node, fileName);
    auto writer = new QXmlStreamWriter(outFile);
    writer->setAutoFormatting(false); // We need a precise handling of line feeds.

    writer->writeStartDocument();
    newLine(*writer);
    writer->writeNamespace(dbNamespace, "db");
    writer->writeNamespace(xlinkNamespace, "xlink");
    writer->writeStartElement(dbNamespace, "article");
    writer->writeAttribute("version", "5.2");
    if (!naturalLanguage.isEmpty())
        writer->writeAttribute("xml:lang", naturalLanguage);
    newLine(*writer);

    // Empty the section stack for the new document.
    sectionLevels.resize(0);
//    sectionLevels.push(1);

    return writer;
}

QXmlStreamWriter *DocBookGenerator::startDocument(const Node *node)
{
    QString fileName = Generator::fileName(node, fileExtension());
    return startGenericDocument(node, fileName);
}

QXmlStreamWriter *DocBookGenerator::startDocument(const ExampleNode *en, const QString &file)
{
    QString fileName = linkForExampleFile(file, en);
    return startGenericDocument(en, fileName);
}

void DocBookGenerator::endDocument(QXmlStreamWriter *writer)
{
    writer->writeEndElement(); // article
    writer->writeEndDocument();
    writer->device()->close();
    delete writer;
}

/*!
  Generate a reference page for the C++ class, namespace, or
  header file documented in \a node.
 */
void DocBookGenerator::generateCppReferencePage(Node *node)
{
    // Based on HtmlGenerator::generateCppReferencePage.
    Q_ASSERT(node->isAggregate());
    const auto aggregate = static_cast<const Aggregate *>(node);

    QString title;
    QString rawTitle;
    QString fullTitle;
    const NamespaceNode *ns = nullptr;
    if (aggregate->isNamespace()) {
        rawTitle = aggregate->plainName();
        fullTitle = aggregate->plainFullName();
        title = rawTitle + " Namespace";
        ns = static_cast<const NamespaceNode *>(aggregate);
    } else if (aggregate->isClass()) {
        rawTitle = aggregate->plainName();
        fullTitle = aggregate->plainFullName();
        title = rawTitle + " Class";
    }

    QString subtitleText;
    if (rawTitle != fullTitle)
        subtitleText = fullTitle;

    // Start producing the DocBook file.
    QXmlStreamWriter &writer = *startDocument(node);

    // Info container.
    generateHeader(writer, title, subtitleText, aggregate);

    generateRequisites(writer, aggregate);
    generateStatus(writer, aggregate);

    // Element synopsis.
    generateDocBookSynopsis(writer, node);

    // Actual content.
    if (!aggregate->doc().isEmpty()) {
        startSection(writer, registerRef("details"), "Detailed Description");

        generateBody(writer, aggregate);
        generateAlsoList(writer, aggregate);
        generateMaintainerList(writer, aggregate);

        endSection(writer);
    }

    Sections sections(const_cast<Aggregate *>(aggregate));
    SectionVector *sectionVector =
            ns ? &sections.stdDetailsSections() : &sections.stdCppClassDetailsSections();
    SectionVector::ConstIterator section = sectionVector->constBegin();
    while (section != sectionVector->constEnd()) {
        bool headerGenerated = false;
        NodeVector::ConstIterator member = section->members().constBegin();
        while (member != section->members().constEnd()) {
            if ((*member)->access() == Node::Private) { // ### check necessary?
                ++member;
                continue;
            }

            if (!headerGenerated) {
                // Equivalent to h2
                startSection(writer, registerRef(section->title().toLower()), section->title());
                headerGenerated = true;
            }

            if ((*member)->nodeType() != Node::Class) {
                // This function starts its own section.
                generateDetailedMember(writer, *member, aggregate);
            } else {
                startSectionBegin(writer);
                writer.writeCharacters("class ");
                generateFullName(writer, *member, aggregate);
                startSectionEnd(writer);
                generateBrief(writer, *member);
                endSection(writer);
            }

            ++member;
        }

        if (headerGenerated)
            endSection(writer);
        ++section;
    }

    generateObsoleteMembers(writer, sections);

    endDocument(&writer);
}

void generateSynopsisInfo(QXmlStreamWriter &writer, const QString &key, const QString &value)
{
    writer.writeStartElement(dbNamespace, "synopsisinfo");
    writer.writeAttribute(dbNamespace, "role", key);
    writer.writeCharacters(value);
    writer.writeEndElement(); // synopsisinfo
    newLine(writer);
}

void generateModifier(QXmlStreamWriter &writer, const QString &value)
{
    writer.writeTextElement(dbNamespace, "modifier", value);
    newLine(writer);
}

/*!
  Generate the metadata for the given \a node in DocBook.
 */
void DocBookGenerator::generateDocBookSynopsis(QXmlStreamWriter &writer, const Node *node)
{
    if (!node)
        return;

    // From Generator::generateStatus, HtmlGenerator::generateRequisites,
    // Generator::generateThreadSafeness, QDocIndexFiles::generateIndexSection.

    // This function is the only place where DocBook extensions are used.
    if (useDocBookExtensions())
        return;

    // Nothing to export in some cases.
    if (node->isGroup() || node->isGroup() || node->isPropertyGroup() || node->isModule()
        || node->isJsModule() || node->isQmlModule() || node->isPageNode())
        return;

    // Cast the node to several subtypes (null pointer if the node is not of the required type).
    const Aggregate *aggregate =
            node->isAggregate() ? static_cast<const Aggregate *>(node) : nullptr;
    const ClassNode *classNode = node->isClass() ? static_cast<const ClassNode *>(node) : nullptr;
    const FunctionNode *functionNode =
            node->isFunction() ? static_cast<const FunctionNode *>(node) : nullptr;
    const PropertyNode *propertyNode =
            node->isProperty() ? static_cast<const PropertyNode *>(node) : nullptr;
    const VariableNode *variableNode =
            node->isVariable() ? static_cast<const VariableNode *>(node) : nullptr;
    const EnumNode *enumNode = node->isEnumType() ? static_cast<const EnumNode *>(node) : nullptr;
    const QmlPropertyNode *qpn =
            node->isQmlProperty() ? static_cast<const QmlPropertyNode *>(node) : nullptr;
    const QmlTypeNode *qcn = node->isQmlType() ? static_cast<const QmlTypeNode *>(node) : nullptr;
    // Typedefs are ignored, as they correspond to enums.
    // Groups and modules are ignored.
    // Documents are ignored, they have no interesting metadata.

    // Start the synopsis tag.
    QString synopsisTag = nodeToSynopsisTag(node);
    writer.writeStartElement(dbNamespace, synopsisTag);
    newLine(writer);

    // Name and basic properties of each tag (like types and parameters).
    if (node->isClass()) {
        writer.writeStartElement(dbNamespace, "ooclass");
        writer.writeTextElement(dbNamespace, "classname", node->plainName());
        writer.writeEndElement(); // ooclass
        newLine(writer);
    } else if (node->isNamespace()) {
        writer.writeTextElement(dbNamespace, "namespacename", node->plainName());
        newLine(writer);
    } else if (node->isQmlType()) {
        writer.writeStartElement(dbNamespace, "ooclass");
        writer.writeTextElement(dbNamespace, "classname", node->plainName());
        writer.writeEndElement(); // ooclass
        newLine(writer);
        if (!qcn->groupNames().isEmpty())
            writer.writeAttribute("groups", qcn->groupNames().join(QLatin1Char(',')));
    } else if (node->isProperty()) {
        writer.writeTextElement(dbNamespace, "modifier", "(Qt property)");
        newLine(writer);
        writer.writeTextElement(dbNamespace, "type", propertyNode->dataType());
        newLine(writer);
        writer.writeTextElement(dbNamespace, "varname", node->plainName());
        newLine(writer);
    } else if (node->isVariable()) {
        if (variableNode->isStatic()) {
            writer.writeTextElement(dbNamespace, "modifier", "static");
            newLine(writer);
        }
        writer.writeTextElement(dbNamespace, "type", variableNode->dataType());
        newLine(writer);
        writer.writeTextElement(dbNamespace, "varname", node->plainName());
        newLine(writer);
    } else if (node->isEnumType()) {
        writer.writeTextElement(dbNamespace, "enumname", node->plainName());
        newLine(writer);
    } else if (node->isQmlProperty()) {
        QString name = node->name();
        if (qpn->isAttached())
            name.prepend(qpn->element() + QLatin1Char('.'));

        writer.writeTextElement(dbNamespace, "type", qpn->dataType());
        newLine(writer);
        writer.writeTextElement(dbNamespace, "varname", name);
        newLine(writer);

        if (qpn->isAttached()) {
            writer.writeTextElement(dbNamespace, "modifier", "attached");
            newLine(writer);
        }
        if ((const_cast<QmlPropertyNode *>(qpn))->isWritable()) {
            writer.writeTextElement(dbNamespace, "modifier", "writable");
            newLine(writer);
        }

        if (qpn->isReadOnly()) {
            generateModifier(writer, "[read-only]");
            newLine(writer);
        }
        if (qpn->isDefault()) {
            generateModifier(writer, "[default]");
            newLine(writer);
        }
    } else if (node->isFunction()) {
        if (functionNode->virtualness() != "non")
            generateModifier(writer, "virtual");
        if (functionNode->isConst())
            generateModifier(writer, "const");
        if (functionNode->isStatic())
            generateModifier(writer, "static");

        if (!functionNode->isMacro()) {
            if (functionNode->returnType() == "void")
                writer.writeEmptyElement(dbNamespace, "void");
            else
                writer.writeTextElement(dbNamespace, "type", functionNode->returnType());
            newLine(writer);
        }
        // Remove two characters from the plain name to only get the name
        // of the method without parentheses.
        writer.writeTextElement(dbNamespace, "methodname", node->plainName().chopped(2));
        newLine(writer);

        if (functionNode->isOverload())
            generateModifier(writer, "overload");
        if (functionNode->isDefault())
            generateModifier(writer, "default");
        if (functionNode->isFinal())
            generateModifier(writer, "final");
        if (functionNode->isOverride())
            generateModifier(writer, "override");

        if (!functionNode->isMacro() && functionNode->parameters().isEmpty()) {
            writer.writeEmptyElement(dbNamespace, "void");
            newLine(writer);
        }

        const Parameters &lp = functionNode->parameters();
        for (int i = 0; i < lp.count(); ++i) {
            const Parameter &parameter = lp.at(i);
            writer.writeStartElement(dbNamespace, "methodparam");
            newLine(writer);
            writer.writeTextElement(dbNamespace, "type", parameter.type());
            newLine(writer);
            writer.writeTextElement(dbNamespace, "parameter", parameter.name());
            newLine(writer);
            if (!parameter.defaultValue().isEmpty()) {
                writer.writeTextElement(dbNamespace, "initializer", parameter.defaultValue());
                newLine(writer);
            }
            writer.writeEndElement(); // methodparam
            newLine(writer);
        }

        generateSynopsisInfo(writer, "meta", functionNode->metanessString());

        if (functionNode->isOverload())
            generateSynopsisInfo(writer, "overload-number",
                                 QString::number(functionNode->overloadNumber()));

        if (functionNode->isRef())
            generateSynopsisInfo(writer, "refness", QString::number(1));
        else if (functionNode->isRefRef())
            generateSynopsisInfo(writer, "refness", QString::number(2));

        if (functionNode->hasAssociatedProperties()) {
            QStringList associatedProperties;
            const NodeList &nodes = functionNode->associatedProperties();
            for (const Node *n : nodes) {
                const auto pn = static_cast<const PropertyNode *>(n);
                associatedProperties << pn->name();
            }
            associatedProperties.sort();
            generateSynopsisInfo(writer, "associated-property",
                                 associatedProperties.join(QLatin1Char(',')));
        }

        QString signature = functionNode->signature(false, false);
        // 'const' is already part of FunctionNode::signature()
        if (functionNode->isFinal())
            signature += " final";
        if (functionNode->isOverride())
            signature += " override";
        if (functionNode->isPureVirtual())
            signature += " = 0";
        else if (functionNode->isDefault())
            signature += " = default";
        generateSynopsisInfo(writer, "signature", signature);
    } else {
        node->doc().location().warning(tr("Unexpected node type in generateDocBookSynopsis: %1")
                                               .arg(node->nodeTypeString()));
        newLine(writer);
    }

    // Accessibility status.
    if (!node->isPageNode() && !node->isCollectionNode()) {
        switch (node->access()) {
        case Node::Public:
            generateSynopsisInfo(writer, "access", "public");
            break;
        case Node::Protected:
            generateSynopsisInfo(writer, "access", "protected");
            break;
        case Node::Private:
            generateSynopsisInfo(writer, "access", "private");
            break;
        default:
            break;
        }
        if (node->isAbstract())
            generateSynopsisInfo(writer, "abstract", "true");
    }

    // Status.
    switch (node->status()) {
    case Node::Active:
        generateSynopsisInfo(writer, "status", "active");
        break;
    case Node::Preliminary:
        generateSynopsisInfo(writer, "status", "preliminary");
        break;
    case Node::Deprecated:
        generateSynopsisInfo(writer, "status", "deprecated");
        break;
    case Node::Obsolete:
        generateSynopsisInfo(writer, "status", "obsolete");
        break;
    case Node::Internal:
        generateSynopsisInfo(writer, "status", "internal");
        break;
    default:
        generateSynopsisInfo(writer, "status", "main");
        break;
    }

    // C++ classes and name spaces.
    if (aggregate) {
        // Includes.
        if (!aggregate->includeFiles().isEmpty()) {
            for (const QString &include : aggregate->includeFiles())
                generateSynopsisInfo(writer, "headers", include);
        }

        // Since and project.
        if (!aggregate->since().isEmpty())
            generateSynopsisInfo(writer, "since", formatSince(aggregate));

        if (aggregate->nodeType() == Node::Class || aggregate->nodeType() == Node::Namespace) {
            // QT variable.
            if (!aggregate->physicalModuleName().isEmpty()) {
                const CollectionNode *cn =
                        qdb_->getCollectionNode(aggregate->physicalModuleName(), Node::Module);
                if (cn && !cn->qtVariable().isEmpty())
                    generateSynopsisInfo(writer, "qmake", "QT += " + cn->qtVariable());
            }
        }

        if (aggregate->nodeType() == Node::Class) {
            // Instantiated by.
            auto *classe = const_cast<ClassNode *>(static_cast<const ClassNode *>(aggregate));
            if (classe->qmlElement() != nullptr && classe->status() != Node::Internal) {
                const Node *otherNode = nullptr;
                Atom a = Atom(Atom::LinkNode, CodeMarker::stringForNode(classe->qmlElement()));
                QString link = getAutoLink(&a, aggregate, &otherNode);

                writer.writeStartElement(dbNamespace, "synopsisinfo");
                writer.writeAttribute(dbNamespace, "role", "instantiatedBy");
                generateSimpleLink(writer, link, classe->qmlElement()->name());
                writer.writeEndElement(); // synopsisinfo
                newLine(writer);
            }

            // Inherits.
            QVector<RelatedClass>::ConstIterator r;
            if (!classe->baseClasses().isEmpty()) {
                writer.writeStartElement(dbNamespace, "synopsisinfo");
                writer.writeAttribute(dbNamespace, "role", "inherits");

                r = classe->baseClasses().constBegin();
                int index = 0;
                while (r != classe->baseClasses().constEnd()) {
                    if ((*r).node_) {
                        generateFullName(writer, (*r).node_, classe);

                        if ((*r).access_ == Node::Protected) {
                            writer.writeCharacters(" (protected)");
                        } else if ((*r).access_ == Node::Private) {
                            writer.writeCharacters(" (private)");
                        }
                        writer.writeCharacters(comma(index++, classe->baseClasses().count()));
                    }
                    ++r;
                }

                writer.writeEndElement(); // synopsisinfo
                newLine(writer);
            }

            // Inherited by.
            if (!classe->derivedClasses().isEmpty()) {
                writer.writeStartElement(dbNamespace, "synopsisinfo");
                writer.writeAttribute(dbNamespace, "role", "inheritedBy");
                generateSortedNames(writer, classe, classe->derivedClasses());
                writer.writeEndElement(); // synopsisinfo
                newLine(writer);
            }
        }
    }

    // QML types.
    if (qcn) {
        // Module name and version (i.e. import).
        QString logicalModuleVersion;
        const CollectionNode *collection =
                qdb_->getCollectionNode(qcn->logicalModuleName(), qcn->nodeType());
        if (collection)
            logicalModuleVersion = collection->logicalModuleVersion();
        else
            logicalModuleVersion = qcn->logicalModuleVersion();

        generateSynopsisInfo(writer, "import",
                             "import " + qcn->logicalModuleName() + QLatin1Char(' ')
                                     + logicalModuleVersion);

        // Since and project.
        if (!qcn->since().isEmpty())
            generateSynopsisInfo(writer, "since", formatSince(qcn));

        // Inherited by.
        NodeList subs;
        QmlTypeNode::subclasses(qcn, subs);
        if (!subs.isEmpty()) {
            writer.writeTextElement(dbNamespace, "synopsisinfo");
            writer.writeAttribute(dbNamespace, "role", "inheritedBy");
            generateSortedQmlNames(writer, qcn, subs);
            writer.writeEndElement(); // synopsisinfo
            newLine(writer);
        }

        // Inherits.
        QmlTypeNode *base = qcn->qmlBaseNode();
        while (base && base->isInternal())
            base = base->qmlBaseNode();
        if (base) {
            const Node *otherNode = nullptr;
            Atom a = Atom(Atom::LinkNode, CodeMarker::stringForNode(base));
            QString link = getAutoLink(&a, base, &otherNode);

            writer.writeTextElement(dbNamespace, "synopsisinfo");
            writer.writeAttribute(dbNamespace, "role", "inherits");
            generateSimpleLink(writer, link, base->name());
            writer.writeEndElement(); // synopsisinfo
            newLine(writer);
        }

        // Instantiates.
        ClassNode *cn = (const_cast<QmlTypeNode *>(qcn))->classNode();
        if (cn && (cn->status() != Node::Internal)) {
            const Node *otherNode = nullptr;
            Atom a = Atom(Atom::LinkNode, CodeMarker::stringForNode(qcn));
            QString link = getAutoLink(&a, cn, &otherNode);

            writer.writeTextElement(dbNamespace, "synopsisinfo");
            writer.writeAttribute(dbNamespace, "role", "instantiates");
            generateSimpleLink(writer, link, cn->name());
            writer.writeEndElement(); // synopsisinfo
            newLine(writer);
        }
    }

    // Thread safeness.
    switch (node->threadSafeness()) {
    case Node::UnspecifiedSafeness:
        generateSynopsisInfo(writer, "threadsafeness", "unspecified");
        break;
    case Node::NonReentrant:
        generateSynopsisInfo(writer, "threadsafeness", "non-reentrant");
        break;
    case Node::Reentrant:
        generateSynopsisInfo(writer, "threadsafeness", "reentrant");
        break;
    case Node::ThreadSafe:
        generateSynopsisInfo(writer, "threadsafeness", "thread safe");
        break;
    default:
        generateSynopsisInfo(writer, "threadsafeness", "unspecified");
        break;
    }

    // Module.
    if (!node->physicalModuleName().isEmpty())
        generateSynopsisInfo(writer, "module", node->physicalModuleName());

    // Group.
    if (classNode && !classNode->groupNames().isEmpty()) {
        generateSynopsisInfo(writer, "groups", classNode->groupNames().join(QLatin1Char(',')));
    } else if (qcn && !qcn->groupNames().isEmpty()) {
        generateSynopsisInfo(writer, "groups", qcn->groupNames().join(QLatin1Char(',')));
    }

    // Properties.
    if (propertyNode) {
        for (const Node *fnNode : propertyNode->getters()) {
            if (fnNode) {
                const auto funcNode = static_cast<const FunctionNode *>(fnNode);
                generateSynopsisInfo(writer, "getter", funcNode->name());
            }
        }
        for (const Node *fnNode : propertyNode->setters()) {
            if (fnNode) {
                const auto funcNode = static_cast<const FunctionNode *>(fnNode);
                generateSynopsisInfo(writer, "setter", funcNode->name());
            }
        }
        for (const Node *fnNode : propertyNode->resetters()) {
            if (fnNode) {
                const auto funcNode = static_cast<const FunctionNode *>(fnNode);
                generateSynopsisInfo(writer, "resetter", funcNode->name());
            }
        }
        for (const Node *fnNode : propertyNode->notifiers()) {
            if (fnNode) {
                const auto funcNode = static_cast<const FunctionNode *>(fnNode);
                generateSynopsisInfo(writer, "notifier", funcNode->name());
            }
        }
    }

    // Enums and typedefs.
    if (enumNode) {
        for (const EnumItem &item : enumNode->items()) {
            writer.writeStartElement(dbNamespace, "enumitem");
            newLine(writer);
            writer.writeAttribute(dbNamespace, "enumidentifier", item.name());
            newLine(writer);
            writer.writeAttribute(dbNamespace, "enumvalue", item.value());
            newLine(writer);
            writer.writeEndElement(); // enumitem
            newLine(writer);
        }
    }

    writer.writeEndElement(); // nodeToSynopsisTag (like classsynopsis)
    newLine(writer);

    // The typedef associated to this enum.
    if (enumNode && enumNode->flagsType()) {
        writer.writeStartElement(dbNamespace, "typedefsynopsis");
        newLine(writer);

        writer.writeTextElement(dbNamespace, "typedefname",
                                enumNode->flagsType()->fullDocumentName());

        writer.writeEndElement(); // typedefsynopsis
        newLine(writer);
    }
}

QString taggedNode(const Node *node)
{
    // From CodeMarker::taggedNode, but without the tag part (i.e. only the QML specific case
    // remaining).
    // TODO: find a better name for this.
    if (node->nodeType() == Node::QmlType && node->name().startsWith(QLatin1String("QML:")))
        return node->name().mid(4);
    return node->name();
}

/*!
  Parses a string with method/variable name and (return) type
  to include type tags.
 */
void DocBookGenerator::typified(QXmlStreamWriter &writer, const QString &string,
                                const Node *relative, bool trailingSpace, bool generateType)
{
    // Adapted from CodeMarker::typified and HtmlGenerator::highlightedCode.
    // Note: CppCodeMarker::markedUpIncludes is not needed for DocBook, as this part is natively
    // generated as DocBook. Hence, there is no need to reimplement <@headerfile> from
    // HtmlGenerator::highlightedCode.
    QString result;
    QString pendingWord;

    for (int i = 0; i <= string.size(); ++i) {
        QChar ch;
        if (i != string.size())
            ch = string.at(i);

        QChar lower = ch.toLower();
        if ((lower >= QLatin1Char('a') && lower <= QLatin1Char('z')) || ch.digitValue() >= 0
            || ch == QLatin1Char('_') || ch == QLatin1Char(':')) {
            pendingWord += ch;
        } else {
            if (!pendingWord.isEmpty()) {
                bool isProbablyType = (pendingWord != QLatin1String("const"));
                if (generateType && isProbablyType) {
                    // Flush the current buffer.
                    writer.writeCharacters(result);
                    result.truncate(0);

                    // Add the link, logic from HtmlGenerator::highlightedCode.
                    const Node *n = qdb_->findTypeNode(pendingWord, relative, Node::DontCare);
                    QString href;
                    if (!(n && (n->isQmlBasicType() || n->isJsBasicType()))
                        || (relative
                            && (relative->genus() == n->genus() || Node::DontCare == n->genus()))) {
                        href = linkForNode(n, relative);
                    }

                    writer.writeStartElement(dbNamespace, "type");
                    if (href.isEmpty())
                        writer.writeCharacters(pendingWord);
                    else
                        generateSimpleLink(writer, href, pendingWord);
                    writer.writeEndElement(); // type
                } else {
                    result += pendingWord;
                }
            }
            pendingWord.clear();

            switch (ch.unicode()) {
            case '\0':
                break;
                // This only breaks out of the switch, not the loop. This means that the loop
                // deliberately overshoots by one character.
            case '&':
                result += QLatin1String("&amp;");
                break;
            case '<':
                result += QLatin1String("&lt;");
                break;
            case '>':
                result += QLatin1String("&gt;");
                break;
            case '\'':
                result += QLatin1String("&apos;");
                break;
            case '"':
                result += QLatin1String("&quot;");
                break;
            default:
                result += ch;
            }
        }
    }

    if (trailingSpace && string.size()) {
        if (!string.endsWith(QLatin1Char('*')) && !string.endsWith(QLatin1Char('&')))
            result += QLatin1Char(' ');
    }

    writer.writeCharacters(result);
}

void DocBookGenerator::generateSynopsisName(QXmlStreamWriter &writer, const Node *node,
                                            const Node *relative, bool generateNameLink)
{
    // Implements the rewriting of <@link> from HtmlGenerator::highlightedCode, only due to calls to
    // CodeMarker::linkTag in CppCodeMarker::markedUpSynopsis.
    QString name = taggedNode(node);

    if (!generateNameLink) {
        writer.writeCharacters(name);
        return;
    }

    writer.writeStartElement(dbNamespace, "emphasis");
    writer.writeAttribute("role", "bold");
    generateSimpleLink(writer, linkForNode(node, relative), name);
    writer.writeEndElement(); // emphasis
}

void DocBookGenerator::generateParameter(QXmlStreamWriter &writer, const Parameter &parameter,
                                         const Node *relative, bool generateExtra,
                                         bool generateType)
{
    const QString &pname = parameter.name();
    const QString &ptype = parameter.type();
    QString paramName;
    if (!pname.isEmpty()) {
        typified(writer, ptype, relative, true, generateType);
        paramName = pname;
    } else {
        paramName = ptype;
    }
    if (generateExtra || pname.isEmpty()) {
        // Look for the _ character in the member name followed by a number (or n):
        // this is intended to be rendered as a subscript.
        QRegExp sub("([a-z]+)_([0-9]+|n)");

        writer.writeStartElement(dbNamespace, "emphasis");
        if (sub.indexIn(paramName) != -1) {
            writer.writeCharacters(sub.cap(0));
            writer.writeStartElement(dbNamespace, "sub");
            writer.writeCharacters(sub.cap(1));
            writer.writeEndElement(); // sub
        } else {
            writer.writeCharacters(paramName);
        }
        writer.writeEndElement(); // emphasis
    }

    const QString &pvalue = parameter.defaultValue();
    if (generateExtra && !pvalue.isEmpty())
        writer.writeCharacters(" = " + pvalue);
}

void DocBookGenerator::generateSynopsis(QXmlStreamWriter &writer, const Node *node,
                                        const Node *relative, Section::Style style)
{
    // From HtmlGenerator::generateSynopsis (conditions written as booleans).
    const bool generateExtra = style != Section::AllMembers;
    const bool generateType = style != Section::Details;
    const bool generateNameLink = style != Section::Details;

    // From CppCodeMarker::markedUpSynopsis, reversed the generation of "extra" and "synopsis".
    const int MaxEnumValues = 6;

    // First generate the extra part if needed (condition from HtmlGenerator::generateSynopsis).
    if (generateExtra) {
        if (node->nodeType() == Node::Function) {
            const auto func = static_cast<const FunctionNode *>(node);
            if (style != Section::Summary && style != Section::Accessors) {
                QStringList bracketed;
                if (func->isStatic()) {
                    bracketed += "static";
                } else if (!func->isNonvirtual()) {
                    if (func->isFinal())
                        bracketed += "final";
                    if (func->isOverride())
                        bracketed += "override";
                    if (func->isPureVirtual())
                        bracketed += "pure";
                    bracketed += "virtual";
                }

                if (func->access() == Node::Protected)
                    bracketed += "protected";
                else if (func->access() == Node::Private)
                    bracketed += "private";

                if (func->isSignal())
                    bracketed += "signal";
                else if (func->isSlot())
                    bracketed += "slot";

                if (!bracketed.isEmpty())
                    writer.writeCharacters(QLatin1Char('[') + bracketed.join(' ')
                                           + QStringLiteral("] "));
            }
        }

        if (style == Section::Summary) {
            QString extra;
            if (node->isPreliminary())
                extra = "(preliminary) ";
            else if (node->isDeprecated())
                extra = "(deprecated) ";
            else if (node->isObsolete())
                extra = "(obsolete) ";

            if (!extra.isEmpty())
                writer.writeCharacters(extra);
        }
    }

    // Then generate the synopsis.
    if (style == Section::Details) {
        if (!node->isRelatedNonmember() && !node->isProxyNode() && !node->parent()->name().isEmpty()
            && !node->parent()->isHeader() && !node->isProperty() && !node->isQmlNode()
            && !node->isJsNode()) {
            writer.writeCharacters(taggedNode(node->parent()) + "::");
        }
    }

    switch (node->nodeType()) {
    case Node::Namespace:
        writer.writeCharacters("namespace ");
        generateSynopsisName(writer, node, relative, generateNameLink);
        break;
    case Node::Class:
        writer.writeCharacters("class ");
        generateSynopsisName(writer, node, relative, generateNameLink);
        break;
    case Node::Function: {
        const auto func = (const FunctionNode *)node;

        // First, the part coming before the name.
        if (style == Section::Summary || style == Section::Accessors) {
            if (!func->isNonvirtual())
                writer.writeCharacters(QStringLiteral("virtual "));
        }

        // Name and parameters.
        if (style != Section::AllMembers && !func->returnType().isEmpty())
            typified(writer, func->returnType(), relative, true, generateType);
        generateSynopsisName(writer, node, relative, generateNameLink);

        if (!func->isMacroWithoutParams()) {
            writer.writeCharacters(QStringLiteral("("));
            if (!func->parameters().isEmpty()) {
                const Parameters &parameters = func->parameters();
                for (int i = 0; i < parameters.count(); i++) {
                    if (i > 0)
                        writer.writeCharacters(QStringLiteral(", "));
                    generateParameter(writer, parameters.at(i), relative, generateExtra,
                                      generateType);
                }
            }
            writer.writeCharacters(QStringLiteral(")"));
        }
        if (func->isConst())
            writer.writeCharacters(QStringLiteral(" const"));

        if (style == Section::Summary || style == Section::Accessors) {
            // virtual is prepended, if needed.
            QString synopsis;
            if (func->isFinal())
                synopsis += QStringLiteral(" final");
            if (func->isOverride())
                synopsis += QStringLiteral(" override");
            if (func->isPureVirtual())
                synopsis += QStringLiteral(" = 0");
            if (func->isRef())
                synopsis += QStringLiteral(" &");
            else if (func->isRefRef())
                synopsis += QStringLiteral(" &&");
            writer.writeCharacters(synopsis);
        } else if (style == Section::AllMembers) {
            if (!func->returnType().isEmpty() && func->returnType() != "void") {
                writer.writeCharacters(QStringLiteral(" : "));
                typified(writer, func->returnType(), relative, false, generateType);
            }
        } else {
            QString synopsis;
            if (func->isRef())
                synopsis += QStringLiteral(" &");
            else if (func->isRefRef())
                synopsis += QStringLiteral(" &&");
            writer.writeCharacters(synopsis);
        }
    } break;
    case Node::Enum: {
        const auto enume = static_cast<const EnumNode *>(node);
        writer.writeCharacters(QStringLiteral("enum "));
        generateSynopsisName(writer, node, relative, generateNameLink);

        QString synopsis;
        if (style == Section::Summary) {
            synopsis += " { ";

            QStringList documentedItems = enume->doc().enumItemNames();
            if (documentedItems.isEmpty()) {
                const auto &enumItems = enume->items();
                for (const auto &item : enumItems)
                    documentedItems << item.name();
            }
            const QStringList omitItems = enume->doc().omitEnumItemNames();
            for (const auto &item : omitItems)
                documentedItems.removeAll(item);

            if (documentedItems.size() > MaxEnumValues) {
                // Take the last element and keep it safe, then elide the surplus.
                const QString last = documentedItems.last();
                documentedItems = documentedItems.mid(0, MaxEnumValues - 1);
                documentedItems += "&#x2026;"; // Ellipsis: in HTML, &hellip;.
                documentedItems += last;
            }
            synopsis += documentedItems.join(QLatin1String(", "));

            if (!documentedItems.isEmpty())
                synopsis += QLatin1Char(' ');
            synopsis += QLatin1Char('}');
        }
        writer.writeCharacters(synopsis);
    } break;
    case Node::Typedef: {
        const auto typedeff = static_cast<const TypedefNode *>(node);
        if (typedeff->associatedEnum())
            writer.writeCharacters("flags ");
        else
            writer.writeCharacters("typedef ");
        generateSynopsisName(writer, node, relative, generateNameLink);
    } break;
    case Node::Property: {
        const auto property = static_cast<const PropertyNode *>(node);
        generateSynopsisName(writer, node, relative, generateNameLink);
        writer.writeCharacters(" : ");
        typified(writer, property->qualifiedDataType(), relative, false, generateType);
    } break;
    case Node::Variable: {
        const auto variable = static_cast<const VariableNode *>(node);
        if (style == Section::AllMembers) {
            generateSynopsisName(writer, node, relative, generateNameLink);
            writer.writeCharacters(" : ");
            typified(writer, variable->dataType(), relative, false, generateType);
        } else {
            typified(writer, variable->leftType(), relative, false, generateType);
            writer.writeCharacters(" ");
            generateSynopsisName(writer, node, relative, generateNameLink);
            writer.writeCharacters(variable->rightType());
        }
    } break;
    default:
        generateSynopsisName(writer, node, relative, generateNameLink);
    }
}

void DocBookGenerator::generateEnumValue(QXmlStreamWriter &writer, const QString &enumValue,
                                         const Node *relative)
{
    // From CppCodeMarker::markedUpEnumValue, simplifications from Generator::plainCode (removing
    // <@op>). With respect to CppCodeMarker::markedUpEnumValue, the order of generation of parents
    // must be reversed so that they are processed in the order
    if (!relative->isEnumType()) {
        writer.writeCharacters(enumValue);
        return;
    }

    QVector<const Node *> parents;
    const Node *node = relative->parent();
    while (node->parent()) {
        parents.prepend(node);
        if (node->parent() == relative || node->parent()->name().isEmpty())
            break;
        node = node->parent();
    }

    writer.writeStartElement(dbNamespace, "code");
    for (auto parent : parents) {
        generateSynopsisName(writer, parent, relative, true);
        writer.writeCharacters("::");
    }
    writer.writeCharacters(enumValue);
    writer.writeEndElement(); // code
}

/*!
  If the node is an overloaded signal, and a node with an
  example on how to connect to it

  Someone didn't finish writing this comment, and I don't know what this
  function is supposed to do, so I have not tried to complete the comment
  yet.
 */
void DocBookGenerator::generateOverloadedSignal(QXmlStreamWriter &writer, const Node *node)
{
    // From Generator::generateOverloadedSignal.
    QString code = getOverloadedSignalCode(node);
    if (code.isEmpty())
        return;

    writer.writeStartElement(dbNamespace, "note");
    newLine(writer);
    writer.writeStartElement(dbNamespace, "para");
    writer.writeCharacters("Signal ");
    writer.writeTextElement(dbNamespace, "emphasis", node->name());
    writer.writeCharacters(" is overloaded in this class. To connect to this "
                           "signal by using the function pointer syntax, Qt "
                           "provides a convenient helper for obtaining the "
                           "function pointer as shown in this example:");
    writer.writeTextElement(dbNamespace, "code", code);
    writer.writeEndElement(); // para
    newLine(writer);
    writer.writeEndElement(); // note
    newLine(writer);
}

/*!
  Generates a bold line that explains that this is a private signal,
  only made public to let users pass it to connect().
 */
void DocBookGenerator::generatePrivateSignalNote(QXmlStreamWriter &writer)
{
    // From Generator::generatePrivateSignalNote.
    writer.writeStartElement(dbNamespace, "note");
    newLine(writer);
    writer.writeTextElement(dbNamespace, "para",
                            "This is a private signal. It can be used in signal connections but "
                            "cannot be emitted by the user.");
    writer.writeEndElement(); // note
    newLine(writer);
}

/*!
  Generates a bold line that says:
  "This function can be invoked via the meta-object system and from QML. See Q_INVOKABLE."
 */
void DocBookGenerator::generateInvokableNote(QXmlStreamWriter &writer, const Node *node)
{
    // From Generator::generateInvokableNote.
    writer.writeStartElement(dbNamespace, "note");
    newLine(writer);
    writer.writeStartElement(dbNamespace, "para");
    writer.writeCharacters(
            "This function can be invoked via the meta-object system and from QML. See ");
    generateSimpleLink(writer, node->url(), "Q_INVOKABLE");
    writer.writeCharacters(".");
    writer.writeEndElement(); // para
    newLine(writer);
    writer.writeEndElement(); // note
    newLine(writer);
}

/*!
  Generates bold Note lines that explain how function \a fn
  is associated with each of its associated properties.
 */
void DocBookGenerator::generateAssociatedPropertyNotes(QXmlStreamWriter &writer,
                                                       const FunctionNode *fn)
{
    // From HtmlGenerator::generateAssociatedPropertyNotes.
    if (fn->hasAssociatedProperties()) {
        writer.writeStartElement(dbNamespace, "note");
        newLine(writer);
        writer.writeStartElement(dbNamespace, "para");

        NodeList nodes = fn->associatedProperties();
        std::sort(nodes.begin(), nodes.end(), Node::nodeNameLessThan);
        for (const auto node : qAsConst(nodes)) {
            QString msg;
            const auto pn = static_cast<const PropertyNode *>(node);
            switch (pn->role(fn)) {
            case PropertyNode::Getter:
                msg = QStringLiteral("Getter function ");
                break;
            case PropertyNode::Setter:
                msg = QStringLiteral("Setter function ");
                break;
            case PropertyNode::Resetter:
                msg = QStringLiteral("Resetter function ");
                break;
            case PropertyNode::Notifier:
                msg = QStringLiteral("Notifier signal ");
                break;
            default:
                break;
            }
            writer.writeCharacters(msg + "for property ");
            generateSimpleLink(writer, linkForNode(pn, nullptr), pn->name());
            writer.writeCharacters(". ");
        }
        writer.writeEndElement(); // para
        newLine(writer);
        writer.writeEndElement(); // note
        newLine(writer);
    }
}

void DocBookGenerator::generateDetailedMember(QXmlStreamWriter &writer, const Node *node,
                                              const PageNode *relative)
{
    // From HtmlGenerator::generateDetailedMember.
    writer.writeStartElement(dbNamespace, "section");
    if (node->isSharedCommentNode()) {
        const auto scn = reinterpret_cast<const SharedCommentNode *>(node);
        const QVector<Node *> &collective = scn->collective();

        bool firstFunction = true;
        for (const Node *n : collective) {
            if (n->isFunction()) {
                QString nodeRef = refForNode(n);

                if (firstFunction) {
                    writer.writeAttribute("xml:id", refForNode(collective.at(0)));
                    newLine(writer);
                    writer.writeStartElement(dbNamespace, "title");
                    generateSynopsis(writer, n, relative, Section::Details);
                    writer.writeEndElement(); // title
                    newLine(writer);

                    firstFunction = false;
                } else {
                    writer.writeStartElement(dbNamespace, "bridgehead");
                    writer.writeAttribute("renderas", "sect2");
                    writer.writeAttribute("xml:id", nodeRef);
                    generateSynopsis(writer, n, relative, Section::Details);
                    writer.writeEndElement(); // bridgehead
                    newLine(writer);
                }
            }
        }
    } else {
        const EnumNode *etn;
        QString nodeRef = refForNode(node);
        if (node->isEnumType() && (etn = static_cast<const EnumNode *>(node))->flagsType()) {
            writer.writeAttribute("xml:id", nodeRef);
            newLine(writer);
            writer.writeStartElement(dbNamespace, "title");
            generateSynopsis(writer, etn, relative, Section::Details);
            writer.writeEndElement(); // title
            newLine(writer);
            writer.writeStartElement(dbNamespace, "bridgehead");
            generateSynopsis(writer, etn->flagsType(), relative, Section::Details);
            writer.writeEndElement(); // bridgehead
            newLine(writer);
        } else {
            writer.writeAttribute("xml:id", nodeRef);
            newLine(writer);
            writer.writeStartElement(dbNamespace, "title");
            generateSynopsis(writer, node, relative, Section::Details);
            writer.writeEndElement(); // title
            newLine(writer);
        }
    }

    generateDocBookSynopsis(writer, node);

    generateStatus(writer, node);
    generateBody(writer, node);
    generateOverloadedSignal(writer, node);
    generateThreadSafeness(writer, node);
    generateSince(writer, node);

    if (node->isProperty()) {
        const auto property = static_cast<const PropertyNode *>(node);
        Section section(Section::Accessors, Section::Active);

        section.appendMembers(property->getters().toVector());
        section.appendMembers(property->setters().toVector());
        section.appendMembers(property->resetters().toVector());

        if (!section.members().isEmpty()) {
            writer.writeStartElement(dbNamespace, "para");
            newLine(writer);
            writer.writeTextElement(dbNamespace, "emphasis", "Access functions:");
            writer.writeAttribute("role", "bold");
            newLine(writer);
            writer.writeEndElement(); // para
            newLine(writer);
            generateSectionList(writer, section, node);
        }

        Section notifiers(Section::Accessors, Section::Active);
        notifiers.appendMembers(property->notifiers().toVector());

        if (!notifiers.members().isEmpty()) {
            writer.writeStartElement(dbNamespace, "para");
            newLine(writer);
            writer.writeTextElement(dbNamespace, "emphasis", "Notifier signal:");
            writer.writeAttribute("role", "bold");
            newLine(writer);
            writer.writeEndElement(); // para
            newLine(writer);
            generateSectionList(writer, notifiers, node);
        }
    } else if (node->isFunction()) {
        const auto fn = static_cast<const FunctionNode *>(node);
        if (fn->isPrivateSignal())
            generatePrivateSignalNote(writer);
        if (fn->isInvokable())
            generateInvokableNote(writer, node);
        generateAssociatedPropertyNotes(writer, fn);
    } else if (node->isEnumType()) {
        const auto en = static_cast<const EnumNode *>(node);

        if (qflagsHref_.isEmpty()) {
            Node *qflags = qdb_->findClassNode(QStringList("QFlags"));
            if (qflags)
                qflagsHref_ = linkForNode(qflags, nullptr);
        }

        if (en->flagsType()) {
            writer.writeStartElement(dbNamespace, "para");
            writer.writeCharacters("The " + en->flagsType()->name() + " type is a typedef for ");
            generateSimpleLink(writer, qflagsHref_, "QFlags");
            writer.writeCharacters("&lt;" + en->name() + "&gt;. ");
            writer.writeCharacters("It stores an OR combination of " + en->name() + "values.");
            writer.writeEndElement(); // para
            newLine(writer);
        }
    }
    generateAlsoList(writer, node);
    endSection(writer); // section
}

void DocBookGenerator::generateSectionList(QXmlStreamWriter &writer, const Section &section,
                                           const Node *relative, Section::Status status)
{
    // From HtmlGenerator::generateSectionList, just generating a list (not tables).
    const NodeVector &members =
            (status == Section::Obsolete ? section.obsoleteMembers() : section.members());
    if (!members.isEmpty()) {
        bool hasPrivateSignals = false;
        bool isInvokable = false;

        writer.writeStartElement(dbNamespace, "itemizedlist");
        newLine(writer);

        int i = 0;
        NodeVector::ConstIterator m = members.constBegin();
        while (m != members.constEnd()) {
            if ((*m)->access() == Node::Private) {
                ++m;
                continue;
            }

            writer.writeStartElement(dbNamespace, "listitem");
            newLine(writer);
            writer.writeStartElement(dbNamespace, "para");

            // prefix no more needed.
            generateSynopsis(writer, *m, relative, section.style());
            if ((*m)->isFunction()) {
                const auto fn = static_cast<const FunctionNode *>(*m);
                if (fn->isPrivateSignal())
                    hasPrivateSignals = true;
                else if (fn->isInvokable())
                    isInvokable = true;
            }

            writer.writeEndElement(); // para
            newLine(writer);
            writer.writeEndElement(); // listitem
            newLine(writer);

            i++;
            ++m;
        }

        writer.writeEndElement(); // itemizedlist
        newLine(writer);

        if (hasPrivateSignals)
            generatePrivateSignalNote(writer);
        if (isInvokable)
            generateInvokableNote(writer, relative);
    }

    if (status != Section::Obsolete && section.style() == Section::Summary
        && !section.inheritedMembers().isEmpty()) {
        writer.writeStartElement(dbNamespace, "itemizedlist");
        newLine(writer);

        generateSectionInheritedList(writer, section, relative);

        writer.writeEndElement(); // itemizedlist
        newLine(writer);
    }
}

void DocBookGenerator::generateSectionInheritedList(QXmlStreamWriter &writer,
                                                    const Section &section, const Node *relative)
{
    // From HtmlGenerator::generateSectionInheritedList.
    QVector<QPair<Aggregate *, int>>::ConstIterator p = section.inheritedMembers().constBegin();
    while (p != section.inheritedMembers().constEnd()) {
        writer.writeStartElement(dbNamespace, "listitem");
        writer.writeCharacters(QString((*p).second) + " ");
        if ((*p).second == 1)
            writer.writeCharacters(section.singular());
        else
            writer.writeCharacters(section.plural());
        writer.writeCharacters(" inherited from ");
        generateSimpleLink(
                writer, fileName((*p).first) + '#' + Generator::cleanRef(section.title().toLower()),
                (*p).first->plainFullName(relative));
        ++p;
    }
}

/*!
  Generate the DocBook page for an entity that doesn't map
  to any underlying parsable C++, QML, or Javascript element.
 */
void DocBookGenerator::generatePageNode(PageNode *pn)
{
    // From HtmlGenerator::generatePageNode, remove anything related to TOCs.
    QXmlStreamWriter &writer = *startDocument(pn);

    generateHeader(writer, pn->fullTitle(), pn->subtitle(), pn);
    generateBody(writer, pn);
    generateAlsoList(writer, pn);
    generateFooter(writer);

    endDocument(&writer);
}

/*!
  Extract sections of markup text and output them.
 */
bool DocBookGenerator::generateQmlText(QXmlStreamWriter &writer, const Text &text,
                                       const Node *relative)
{
    // From Generator::generateQmlText.
    const Atom *atom = text.firstAtom();
    bool result = false;

    if (atom != nullptr) {
        initializeTextOutput();
        while (atom) {
            if (atom->type() != Atom::QmlText)
                atom = atom->next();
            else {
                atom = atom->next();
                while (atom && (atom->type() != Atom::EndQmlText)) {
                    int n = 1 + generateAtom(writer, atom, relative);
                    while (n-- > 0)
                        atom = atom->next();
                }
            }
        }
        result = true;
    }
    return result;
}

/*!
  Generate the DocBook page for a QML type. \qcn is the QML type.
 */
void DocBookGenerator::generateQmlTypePage(QmlTypeNode *qcn)
{
    // From HtmlGenerator::generateQmlTypePage.
    // Start producing the DocBook file.
    QXmlStreamWriter &writer = *startDocument(qcn);

    Generator::setQmlTypeContext(qcn);
    QString title = qcn->fullTitle();
    if (qcn->isJsType())
        title += " JavaScript Type";
    else
        title += " QML Type";

    generateHeader(writer, title, qcn->subtitle(), qcn);
    generateQmlRequisites(writer, qcn);

    startSection(writer, registerRef("details"), "Detailed Description");
    generateBody(writer, qcn);

    ClassNode *cn = qcn->classNode();
    if (cn)
        generateQmlText(writer, cn->doc().body(), cn);
    generateAlsoList(writer, qcn);

    endSection(writer);

    Sections sections(qcn);
    for (const auto &section : sections.stdQmlTypeDetailsSections()) {
        if (!section.isEmpty()) {
            startSection(writer, registerRef(section.title().toLower()), section.title());

            for (const auto &member : section.members())
                generateDetailedQmlMember(writer, member, qcn);

            endSection(writer);
        }
    }

    generateObsoleteQmlMembers(writer, sections);

    generateFooter(writer);
    Generator::setQmlTypeContext(nullptr);

    endDocument(&writer);
}

/*!
  Generate the DocBook page for the QML basic type represented
  by the QML basic type node \a qbtn.
 */
void DocBookGenerator::generateQmlBasicTypePage(QmlBasicTypeNode *qbtn)
{
    // From HtmlGenerator::generateQmlBasicTypePage.
    // Start producing the DocBook file.
    QXmlStreamWriter &writer = *startDocument(qbtn);

    QString htmlTitle = qbtn->fullTitle();
    if (qbtn->isJsType())
        htmlTitle += " JavaScript Basic Type";
    else
        htmlTitle += " QML Basic Type";

    Sections sections(qbtn);
    generateHeader(writer, htmlTitle, qbtn->subtitle(), qbtn);

    startSection(writer, registerRef("details"), "Detailed Description");

    generateBody(writer, qbtn);
    generateAlsoList(writer, qbtn);

    endSection(writer);

    SectionVector::ConstIterator s = sections.stdQmlTypeDetailsSections().constBegin();
    while (s != sections.stdQmlTypeDetailsSections().constEnd()) {
        if (!s->isEmpty()) {
            startSection(writer, registerRef(s->title().toLower()), s->title());

            NodeVector::ConstIterator m = s->members().constBegin();
            while (m != s->members().constEnd()) {
                generateDetailedQmlMember(writer, *m, qbtn);
                ++m;
            }

            endSection(writer);
        }
        ++s;
    }
    generateFooter(writer);

    endDocument(&writer);
}

/*!
  Outputs the DocBook detailed documentation for a section
  on a QML element reference page.
 */
void DocBookGenerator::generateDetailedQmlMember(QXmlStreamWriter &writer, Node *node,
                                                 const Aggregate *relative)
{
    // From HtmlGenerator::generateDetailedQmlMember, with elements from
    // CppCodeMarker::markedUpQmlItem and HtmlGenerator::generateQmlItem.
    std::function<QString(QmlPropertyNode *)> getQmlPropertyTitle = [&](QmlPropertyNode *n) {
        if (!n->isReadOnlySet() && n->declarativeCppNode())
            n->markReadOnly(!n->isWritable());

        QString title;
        if (!n->isWritable())
            title += "[read-only] ";
        if (n->isDefault())
            title += "[default] ";

        // Finalise generation of name, as per CppCodeMarker::markedUpQmlItem.
        if (n->isAttached())
            title += n->element() + QLatin1Char('.');
        title += n->name() + " : " + n->dataType();

        return title;
    };

    std::function<void(QXmlStreamWriter &, Node *)> generateQmlMethodTitle =
            [&](QXmlStreamWriter &w, Node *n) {
                generateSynopsis(w, n, relative, Section::Details);
            };

    bool generateEndSection = true;

    if (node->isPropertyGroup()) {
        const auto scn = static_cast<const SharedCommentNode *>(node);

        QString heading;
        if (!scn->name().isEmpty())
            heading = scn->name() + " group";
        else
            heading = node->name();
        startSection(writer, refForNode(scn), heading);
        // This last call creates a title for this section. In other words,
        // titles are forbidden for the rest of the section.

        const QVector<Node *> sharedNodes = scn->collective();
        for (const auto &node : sharedNodes) {
            if (node->isQmlProperty() || node->isJsProperty()) {
                auto *qpn = static_cast<QmlPropertyNode *>(node);

                writer.writeStartElement(dbNamespace, "bridgehead");
                writer.writeAttribute("renderas", "sect2");
                writer.writeAttribute("xml:id", refForNode(qpn));
                writer.writeCharacters(getQmlPropertyTitle(qpn));
                writer.writeEndElement(); // bridgehead
                newLine(writer);

                generateDocBookSynopsis(writer, qpn);
            }
        }
    } else if (node->isQmlProperty() || node->isJsProperty()) {
        auto qpn = static_cast<QmlPropertyNode *>(node);
        startSection(writer, refForNode(qpn), getQmlPropertyTitle(qpn));
        generateDocBookSynopsis(writer, qpn);
    } else if (node->isSharedCommentNode()) {
        const auto scn = reinterpret_cast<const SharedCommentNode *>(node);
        const QVector<Node *> &sharedNodes = scn->collective();

        // In the section, generate a title for the first node, then bridgeheads for
        // the next ones.
        int i = 0;
        for (const auto m : sharedNodes) {
            // Ignore this element if there is nothing to generate.
            if (!node->isFunction(Node::QML) && !node->isFunction(Node::JS)
                && !node->isQmlProperty() && !node->isJsProperty()) {
                continue;
            }

            // Complete the section tag.
            if (i == 0) {
                writer.writeStartElement(dbNamespace, "section");
                writer.writeAttribute("xml:id", refForNode(m));
                newLine(writer);
            }

            // Write the tag containing the title.
            writer.writeStartElement(dbNamespace, (i == 0) ? "title" : "bridgehead");
            if (i > 0)
                writer.writeAttribute("renderas", "sect2");

            // Write the title.
            QString title;
            if (node->isFunction(Node::QML) || node->isFunction(Node::JS))
                generateQmlMethodTitle(writer, node);
            else if (node->isQmlProperty() || node->isJsProperty())
                writer.writeCharacters(getQmlPropertyTitle(static_cast<QmlPropertyNode *>(node)));

            // Complete the title and the synopsis.
            generateDocBookSynopsis(writer, m);
            ++i;
        }

        if (i == 0)
            generateEndSection = false;
    } else { // assume the node is a method/signal handler
        startSectionBegin(writer, refForNode(node));
        generateQmlMethodTitle(writer, node);
        startSectionEnd(writer);
    }

    generateStatus(writer, node);
    generateBody(writer, node);
    generateThreadSafeness(writer, node);
    generateSince(writer, node);
    generateAlsoList(writer, node);

    if (generateEndSection)
        endSection(writer);
}

/*!
  Recursive writing of DocBook files from the root \a node.
 */
void DocBookGenerator::generateDocumentation(Node *node)
{
    // Mainly from Generator::generateDocumentation, with parts from
    // Generator::generateDocumentation and WebXMLGenerator::generateDocumentation.
    // Don't generate nodes that are already processed, or if they're not
    // supposed to generate output, ie. external, index or images nodes.
    if (!node->url().isNull())
        return;
    if (node->isIndexNode())
        return;
    if (node->isInternal() && !showInternal_)
        return;
    if (node->isExternalPage())
        return;

    if (node->parent()) {
        if (node->isCollectionNode()) {
            /*
              A collection node collects: groups, C++ modules,
              QML modules or JavaScript modules. Testing for a
              CollectionNode must be done before testing for a
              TextPageNode because a CollectionNode is a PageNode
              at this point.

              Don't output an HTML page for the collection
              node unless the \group, \module, \qmlmodule or
              \jsmodule command was actually seen by qdoc in
              the qdoc comment for the node.

              A key prerequisite in this case is the call to
              mergeCollections(cn). We must determine whether
              this group, module, QML module, or JavaScript
              module has members in other modules. We know at
              this point that cn's members list contains only
              members in the current module. Therefore, before
              outputting the page for cn, we must search for
              members of cn in the other modules and add them
              to the members list.
            */
            auto cn = static_cast<CollectionNode *>(node);
            if (cn->wasSeen()) {
                qdb_->mergeCollections(cn);
                generateCollectionNode(cn);
            } else if (cn->isGenericCollection()) {
                // Currently used only for the module's related orphans page
                // but can be generalized for other kinds of collections if
                // other use cases pop up.
                generateGenericCollectionPage(cn);
            }
        } else if (node->isTextPageNode()) { // Pages.
            generatePageNode(static_cast<PageNode *>(node));
        } else if (node->isAggregate()) { // Aggregates.
            if ((node->isClassNode() || node->isHeader() || node->isNamespace())
                && node->docMustBeGenerated()) {
                generateCppReferencePage(static_cast<Aggregate *>(node));
            } else if (node->isQmlType() || node->isJsType()) {
                generateQmlTypePage(static_cast<QmlTypeNode *>(node));
            } else if (node->isQmlBasicType() || node->isJsBasicType()) {
                generateQmlBasicTypePage(static_cast<QmlBasicTypeNode *>(node));
            } else if (node->isProxyNode()) {
                generateProxyPage(static_cast<Aggregate *>(node));
            }
        }
    }

    if (node->isAggregate()) {
        auto *aggregate = static_cast<Aggregate *>(node);
        for (auto c : aggregate->childNodes()) {
            if (node->isPageNode() && !node->isPrivate())
                generateDocumentation(c);
        }
    }
}

void DocBookGenerator::generateProxyPage(Aggregate *aggregate)
{
    // Adapted from HtmlGenerator::generateProxyPage.
    Q_ASSERT(aggregate->isProxyNode());

    // Start producing the DocBook file.
    QXmlStreamWriter &writer = *startDocument(aggregate);

    // Info container.
    generateHeader(writer, aggregate->plainFullName(), "", aggregate);

    // No element synopsis.

    // Actual content.
    if (!aggregate->doc().isEmpty()) {
        startSection(writer, registerRef("details"), "Detailed Description");

        generateBody(writer, aggregate);
        generateAlsoList(writer, aggregate);
        generateMaintainerList(writer, aggregate);

        endSection(writer);
    }

    Sections sections(aggregate);
    SectionVector *detailsSections = &sections.stdDetailsSections();

    for (const auto &section : qAsConst(*detailsSections)) {
        if (section.isEmpty())
            continue;

        startSection(writer, section.title().toLower(), section.title());

        const QVector<Node *> &members = section.members();
        for (const auto &member : members) {
            if (!member->isPrivate()) { // ### check necessary?
                if (!member->isClassNode()) {
                    generateDetailedMember(writer, member, aggregate);
                } else {
                    startSectionBegin(writer);
                    generateFullName(writer, member, aggregate);
                    startSectionEnd(writer);
                    generateBrief(writer, member);
                    endSection(writer);
                }
            }
        }

        endSection(writer);
    }

    generateFooter(writer);

    endDocument(&writer);
}

/*!
  Generate the HTML page for a group, module, or QML module.
 */
void DocBookGenerator::generateCollectionNode(CollectionNode *cn)
{
    // Adapted from HtmlGenerator::generateCollectionNode.
    // Start producing the DocBook file.
    QXmlStreamWriter &writer = *startDocument(cn);

    // Info container.
    generateHeader(writer, cn->fullTitle(), cn->subtitle(), cn);

    // Element synopsis.
    generateDocBookSynopsis(writer, cn);

    // Actual content.
    if (cn->isModule()) {
        // Generate brief text and status for modules.
        generateBrief(writer, cn);
        generateStatus(writer, cn);
        generateSince(writer, cn);

        NodeMultiMap nmm;
        cn->getMemberNamespaces(nmm);
        if (!nmm.isEmpty()) {
            startSection(writer, registerRef("namespaces"), "Namespaces");
            generateAnnotatedList(writer, cn, nmm, "namespaces");
            endSection(writer);
        }
        nmm.clear();
        cn->getMemberClasses(nmm);
        if (!nmm.isEmpty()) {
            startSection(writer, registerRef("classes"), "Classes");
            generateAnnotatedList(writer, cn, nmm, "classes");
            endSection(writer);
        }
        nmm.clear();
    }

    Text brief = cn->doc().briefText();
    bool generatedTitle = false;
    if (cn->isModule() && !brief.isEmpty()) {
        startSection(writer, registerRef("details"), "Detailed Description");
        generatedTitle = true;
    } else {
        writeAnchor(writer, registerRef("details"));
    }

    generateBody(writer, cn);
    generateAlsoList(writer, cn);

    if (!cn->noAutoList() && (cn->isGroup() || cn->isQmlModule() || cn->isJsModule()))
        generateAnnotatedList(writer, cn, cn->members(), "members");

    if (generatedTitle)
        endSection(writer);

    generateFooter(writer);

    endDocument(&writer);
}

/*!
  Generate the HTML page for a generic collection. This is usually
  a collection of C++ elements that are related to an element in
  a different module.
 */
void DocBookGenerator::generateGenericCollectionPage(CollectionNode *cn)
{
    // Adapted from HtmlGenerator::generateGenericCollectionPage.
    // TODO: factor out this code to generate a file name.
    QString name = cn->name().toLower();
    name.replace(QChar(' '), QString("-"));
    QString filename =
        cn->tree()->physicalModuleName() + "-" + name + "." + fileExtension();

    // Start producing the DocBook file.
    QXmlStreamWriter &writer = *startGenericDocument(cn, filename);

    // Info container.
    generateHeader(writer, cn->fullTitle(), cn->subtitle(), cn);

    // Element synopsis.
    generateDocBookSynopsis(writer, cn);

    // Actual content.
    writer.writeStartElement(dbNamespace, "para");
    writer.writeCharacters("Each function or type documented here is related to a class or "
                            "namespace that is documented in a different module. The reference "
                            "page for that class or namespace will link to the function or type "
                            "on this page.");
    writer.writeEndElement(); // para

    const CollectionNode *cnc = cn;
    const QList<Node *> members = cn->members();
    for (const auto &member : members)
        generateDetailedMember(writer, member, cnc);

    generateFooter(writer);

    endDocument(&writer);
}

void DocBookGenerator::generateFullName(QXmlStreamWriter &writer, const Node *node,
                                        const Node *relative)
{
    // From Generator::appendFullName.
    writer.writeStartElement(dbNamespace, "link");
    writer.writeAttribute(xlinkNamespace, "href", fullDocumentLocation(node));
    writer.writeAttribute(xlinkNamespace, "role", targetType(node));
    writer.writeCharacters(node->fullName(relative));
    writer.writeEndElement(); // link
}

void DocBookGenerator::generateFullName(QXmlStreamWriter &writer, const Node *apparentNode,
                                        const QString &fullName, const Node *actualNode)
{
    // From Generator::appendFullName.
    if (actualNode == nullptr)
        actualNode = apparentNode;
    writer.writeStartElement(dbNamespace, "link");
    writer.writeAttribute(xlinkNamespace, "href", fullDocumentLocation(actualNode));
    writer.writeAttribute("type", targetType(actualNode));
    writer.writeCharacters(fullName);
    writer.writeEndElement(); // link
}

QT_END_NAMESPACE