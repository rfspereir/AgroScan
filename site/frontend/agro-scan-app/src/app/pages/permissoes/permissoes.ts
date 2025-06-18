import { Component, OnInit } from '@angular/core';
import { CommonModule } from '@angular/common';
import { RouterModule } from '@angular/router';
import { Topbar } from '../../shared/topbar/topbar';
import { Database, ref, get, child, set, remove, update } from '@angular/fire/database';
import { FormsModule } from '@angular/forms';


@Component({
  selector: 'app-permissoes',
  standalone: true,
  imports: [CommonModule, RouterModule, Topbar, FormsModule],
  templateUrl: './permissoes.html',
  styleUrls: ['./permissoes.css']
})
export class Permissoes implements OnInit {
  permissoesDisponiveis = [
    'gerenciar_usuarios_cliente',
    'gerenciar_dispositivos_cliente',
    'visualizar_dados_cliente',
    'consultar_status_dispositivo',
    'checklist',
    'upload_observacoes',
    'gerar_relatorios',
    'exportar_dados',
    'visualizar_logs',
    'configurar_alertas',
    'administrar_sistema'
  ];

  grupos: any[] = [];

  novoGrupoNome = '';
  novoGrupoDescricao = '';
  novoGrupoPermissoes: string[] = [];

  constructor(private db: Database) {}

  ngOnInit(): void {
    this.carregarGrupos();
  }

  async carregarGrupos() {
    const dbRef = ref(this.db);
    try {
      const snapshot = await get(child(dbRef, 'permissoes'));
      if (snapshot.exists()) {
        const dados = snapshot.val();
        this.grupos = Object.keys(dados).map((key) => ({
          id: key,
          ...dados[key]
        }));
      } else {
        this.grupos = [];
      }
    } catch (error) {
      console.error('Erro ao carregar grupos:', error);
    }
  }

  async adicionarGrupo() {
    if (!this.novoGrupoNome.trim()) return;

    await set(ref(this.db, `permissoes/${this.novoGrupoNome.trim()}`), {
      descricao: this.novoGrupoDescricao.trim(),
      permissoes: this.novoGrupoPermissoes
    });

    this.novoGrupoNome = '';
    this.novoGrupoDescricao = '';
    this.novoGrupoPermissoes = [];

    this.carregarGrupos();
  }

  async removerGrupo(id: string) {
    await remove(ref(this.db, `permissoes/${id}`));
    this.carregarGrupos();
  }

  async editarGrupo(grupo: any) {
    const atualizacao = {
      descricao: grupo.novoDescricao.trim(),
      permissoes: grupo.novoPermissoes
    };

    await update(ref(this.db, `permissoes/${grupo.id}`), atualizacao);

    grupo.editando = false;
    this.carregarGrupos();
  }

  habilitarEdicao(grupo: any) {
    grupo.editando = true;
    grupo.novoDescricao = grupo.descricao;
    grupo.novoPermissoes = [...grupo.permissoes];
  }

  cancelarEdicao(grupo: any) {
    grupo.editando = false;
  }

  togglePermissao(permissao: string, lista: string[]) {
  const index = lista.indexOf(permissao);
  if (index >= 0) {
    lista.splice(index, 1);
  } else {
    lista.push(permissao);
  }
}

}

