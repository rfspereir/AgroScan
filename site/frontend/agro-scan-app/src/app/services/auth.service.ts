import { Injectable } from '@angular/core';
import { Auth, signInWithEmailAndPassword, setPersistence, browserLocalPersistence, signOut, User, onAuthStateChanged, getIdTokenResult } from '@angular/fire/auth';
import { BehaviorSubject } from 'rxjs';

@Injectable({
  providedIn: 'root'
})
export class AuthService {

  private currentUser: User | null = null;

  private permissoesSubject = new BehaviorSubject<string[]>([]);  
  permissoes$ = this.permissoesSubject.asObservable();

  private clienteIdSubject = new BehaviorSubject<string | null>(null);
  clienteId$ = this.clienteIdSubject.asObservable();

  private roleSubject = new BehaviorSubject<string | null>(null);
  role$ = this.roleSubject.asObservable();

  constructor(private auth: Auth) {
    onAuthStateChanged(this.auth, async (user) => {
      this.currentUser = user;
      if (user) {
        await this.loadCustomClaims();
      } else {
        this.clienteIdSubject.next(null);
        this.roleSubject.next(null);
      }
    });
  }

  async login(email: string, password: string) {
    return signInWithEmailAndPassword(this.auth, email, password)
      .then(async (userCredential) => {
        await setPersistence(this.auth, browserLocalPersistence);
        console.log('Login bem-sucedido!', userCredential);
      })
      .catch((error) => {
        console.error('Erro no login:', error);
      });
  }

  async logout() {
    await signOut(this.auth);
    this.currentUser = null;
    this.clienteIdSubject.next(null);
    this.roleSubject.next(null);
  }

  public async loadCustomClaims() {
    if (!this.currentUser) return;

    const idTokenResult = await this.currentUser.getIdTokenResult(true);
    const claims = idTokenResult.claims;

    const clienteId: string | null = typeof claims['clienteId'] === 'string' ? claims['clienteId'] : null;
    const role: string | null = typeof claims['role'] === 'string' ? claims['role'] : null;

    this.clienteIdSubject.next(clienteId);
    this.roleSubject.next(role);

    console.log('Claims carregados:', { clienteId, role });
  }

  getPermissoes() {
    return this.permissoesSubject.value;
  }

  hasPermissao(permissao: string): boolean {
    return this.permissoesSubject.value.includes(permissao);
  }

  getClienteId() {
    return this.clienteIdSubject.value;
  }

  getRole() {
    return this.roleSubject.value;
  }

  getCurrentUser() {
    return this.currentUser;
  }

  isLoggedIn(): boolean {
    return this.currentUser !== null;
  }
}
